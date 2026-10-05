cbuffer CameraData : register(b0, space3)
{
	float4 positionAndScale; // xyz camera position, w tan(fovY/2)
	float4 rightAndAspect; // xyz camera right, w width/height
	float4 up;
	float4 forward;
	float4 shadingA; // x normal radius, y normal fade, z axis tint, w smooth normals on
	float4 shadingB; // x concavity guard, y pixel angle at unit distance
};

cbuffer PaletteData : register(b1, space3)
{
	float4 paletteColours[256];
};

// Lights and atmosphere share one buffer because SDL_GPU allows only four per stage
cbuffer EnvironmentData : register(b2, space3)
{
	float4 keyDirection; // xyz unit direction towards the key, w intensity
	float4 keyColour; // xyz linear colour
	float4 skyColour; // xyz linear colour, w intensity
	float4 groundColour; // xyz linear colour, w intensity
	float4 ambientSettings; // x occlusion floor

	float4 skyHorizon; // xyz linear colour, w fog density
	float4 skyZenith; // xyz linear colour, w ground tile size in metres
	float4 groundLight; // xyz linear colour, w ground plane on
	float4 groundDark; // xyz linear colour
};

cbuffer SceneData : register(b3, space3)
{
	float4 counts; // x instance count
	float4 modelOffset[8]; // xyz atlas offset in voxels, w voxel size in metres
	float4 modelSize[8]; // xyz size in voxels
	float4 instanceTransform[16]; // xyz position in metres, w model index
	float4 instanceRotation[16]; // x sine of yaw, y cosine of yaw, z scale
};


Texture3D<uint> voxels : register(t0, space2);

static const int maxSteps = 768;

struct PSInput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

struct BoxHit
{
	bool hit;
	float tEnter;
	float tExit;
	float3 entryNormal;
};

BoxHit IntersectBox(float3 in_origin, float3 in_direction, float3 in_boxMin, float3 in_boxMax)
{
	// A division by a zero direction component gives +-infinity
	// which compares correctly in the min/max below, so parallel rays need no special case
	const float3 inverseDirection = 1.0f / in_direction;

	const float3 t1 = (in_boxMin - in_origin) * inverseDirection;
	const float3 t2 = (in_boxMax - in_origin) * inverseDirection;

	const float3 tNear = min(t1, t2);
	const float3 tFar = max(t1, t2);

	BoxHit result;
	result.tEnter = max(max(tNear.x, tNear.y), tNear.z);
	result.tExit = min(min(tFar.x, tFar.y), tFar.z);
	result.hit = result.tExit >= result.tEnter && result.tExit >= 0.0f;

	// The axis whose near plane produced tEnter is the face entered through,
	// which is the normal for a voxel that is solid before any step is taken
	const float3 entryAxis = step(result.tEnter.xxx, tNear);
	result.entryNormal = -sign(in_direction) * entryAxis;

	return result;
}

// Every model lives in one 3D texture at its own offset, so a lookup is a local voxel plus that
// offset. The size is the model's own, which is what keeps one model from reading into the next
struct Model
{
	int3 offset;
	int3 size;
	float voxelSize;
};

Model GetModel(int in_index)
{
	Model model;
	model.offset = (int3)modelOffset[in_index].xyz;
	model.size = (int3)modelSize[in_index].xyz;
	model.voxelSize = modelOffset[in_index].w;

	return model;
}

struct Instance
{
	float3 position;
	float sinYaw;
	float cosYaw;
	float scale;
	int model;
};

Instance GetInstance(int in_index)
{
	Instance instance;
	instance.position = instanceTransform[in_index].xyz;
	instance.model = (int)instanceTransform[in_index].w;
	instance.sinYaw = instanceRotation[in_index].x;
	instance.cosYaw = instanceRotation[in_index].y;
	instance.scale = instanceRotation[in_index].z;

	return instance;
}

// The inverse of a yaw rotation is the same rotation with the sine negated, so one pair of
// functions covers both directions
float3 WorldToLocal(Instance in_instance, float3 in_direction)
{
	return float3(
		in_instance.cosYaw * in_direction.x - in_instance.sinYaw * in_direction.z,
		in_direction.y,
		in_instance.sinYaw * in_direction.x + in_instance.cosYaw * in_direction.z
	);
}

float3 LocalToWorld(Instance in_instance, float3 in_direction)
{
	return float3(
		in_instance.cosYaw * in_direction.x + in_instance.sinYaw * in_direction.z,
		in_direction.y,
		-in_instance.sinYaw * in_direction.x + in_instance.cosYaw * in_direction.z
	);
}

// 0 is air, and also what out-of-bounds returns: Load outside the texture is
// undefined rather than zero, so the bounds check stays
uint SampleVoxel(Model in_model, int3 in_voxel)
{
	if (any(in_voxel < 0) || any(in_voxel >= in_model.size))
	{
		return 0;
	}

	return voxels.Load(int4(in_model.offset + in_voxel, 0));
}

// "Solidity" as a continuous field rather than per voxel
// Trilinear between the eight voxel centres around the point. The gradient of a binary field is a face
// normal, which is what we already have.
float Occupancy(Model in_model, float3 in_point)
{
	// Voxel v covers [v, v+1), so its centre is at v + 0.5
	const float3 shifted = in_point - 0.5f;
	const int3 base = (int3)floor(shifted);
	const float3 fraction = shifted - (float3)base;

	float result = 0.0f;

	[unroll]
	for (int corner = 0; corner < 8; ++corner)
	{
		const int3 offset = int3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
		const float3 weight = lerp(1.0f - fraction, fraction, (float3)offset);

		result += weight.x * weight.y * weight.z * (SampleVoxel(in_model, base + offset) != 0 ? 1.0f : 0.0f);
	}

	return result;
}

// A shading normal that follows the shape a group of voxels describes rather than
// the face the ray happened to enter.
// Basically low voxel density stops using whole faces (if that makes sense? no?)
float3 ShadingNormal(Model in_model, float3 in_point, float3 in_faceNormal, float in_voxelsPerPixel)
{
	// Far away a voxel is smaller than a pixel and its facets are noise which should be removed
	// Close up a voxel is several pixels of actual geometry - so a real face
	// Fading between the two is also what puts the three-axis voxel cue back at exactly the range someone looks for it.
	const float fadeMid = max(shadingA.y, 0.0001f);

	float weight = smoothstep(fadeMid * 0.5f, fadeMid * 1.5f, in_voxelsPerPixel);

	if (weight <= 0.0f)
	{
		return in_faceNormal;
	}

	const float radius = shadingA.x;

	const float xPlus = Occupancy(in_model, in_point + float3(radius, 0.0f, 0.0f));
	const float xMinus = Occupancy(in_model, in_point - float3(radius, 0.0f, 0.0f));
	const float yPlus = Occupancy(in_model, in_point + float3(0.0f, radius, 0.0f));
	const float yMinus = Occupancy(in_model, in_point - float3(0.0f, radius, 0.0f));
	const float zPlus = Occupancy(in_model, in_point + float3(0.0f, 0.0f, radius));
	const float zMinus = Occupancy(in_model, in_point - float3(0.0f, 0.0f, radius));

	// A flat or convex surface has solid on one side only, so the six probes sum to ~1
	// More than that is a concave corner (e.g., floor meeting a wall) where smoothing bleeds
	// one surface into the other and lights a fillet neither has
	const float total = xPlus + xMinus + yPlus + yMinus + zPlus + zMinus;
	const float concavity = saturate((total - 1.2f) / 0.8f);

	weight *= saturate(1.0f - concavity * shadingB.x);

	if (weight <= 0.0f)
	{
		return in_faceNormal;
	}

	const float3 gradient = float3(xPlus - xMinus, yPlus - yMinus, zPlus - zMinus);
	const float magnitude = length(gradient);

	// A uniform neighbourhood (flat face) has no gradient at all.
	// A wall one voxel thick is air on both sides, so there is
	// nothing to measure and the face normal stands
	if (magnitude < 0.0001f)
	{
		return in_faceNormal;
	}

	return normalize(lerp(in_faceNormal, -gradient / magnitude, weight));
}

// Different axis directions have three different lighting values to help show the voxels
// It uses the geometric normal so smoothing doesn't break it.
float AxisTint(float3 in_faceNormal)
{
	// A DDA face normal is axis aligned, so exactly one of these is 1
	return 1.06f * saturate(in_faceNormal.y)
		+ 0.72f * saturate(-in_faceNormal.y)
		+ 0.88f * abs(in_faceNormal.x)
		+ 0.97f * abs(in_faceNormal.z);
}

// Occlusion gathered over a radius measured in voxels rather than per-face, so the darkening
// is wide enough to read at a camera where one voxel is about one pixel.
// Per-voxel corner AO is sub-pixel detail at this density
static const float aoRadius = 12.0f; // voxels
static const int aoDirections = 5;
static const int aoSamples = 4;

float VoxelAmbientOcclusion(Model in_model, float3 in_hitPoint, float3 in_normal)
{
	// Directions within a hemisphere - one straight out, four tilted
	// Enough for a soft result without the variance that would need dithering.
	float3 tangent = abs(in_normal.y) > 0.9f ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
	tangent = normalize(tangent - in_normal * dot(tangent, in_normal));

	const float3 bitangent = cross(in_normal, tangent);

	const float3 directions[aoDirections] = {
		in_normal,
		normalize(in_normal * 0.7f + tangent * 0.7f),
		normalize(in_normal * 0.7f - tangent * 0.7f),
		normalize(in_normal * 0.7f + bitangent * 0.7f),
		normalize(in_normal * 0.7f - bitangent * 0.7f)
	};

	float occlusion = 0.0f;

	for (int d = 0; d < aoDirections; ++d)
	{
		for (int s = 0; s < aoSamples; ++s)
		{
			// Samples bunch up near the surface, where occluders matter most
			const float distance = aoRadius * pow((s + 1.0f) / aoSamples, 2.0f);
			const int3 sampleVoxel = (int3)floor(in_hitPoint + in_normal * 0.5f + directions[d] * distance);

			if (SampleVoxel(in_model, sampleVoxel) != 0)
			{
				// Nearer occluders darken more
				occlusion += 1.0f - distance / aoRadius;
			}
		}
	}

	const float maximum = aoDirections * aoSamples * 0.5f;

	return saturate(1.0f - occlusion / maximum);
}

struct TraceResult
{
	uint material;
	float3 normal;
	float t;
	int3 voxel;
};

// Amanatides and Woo, in grid space where one voxel = one unit
TraceResult TraceGrid(Model in_model, float3 in_origin, float3 in_direction, float in_tStart, float in_tEnd, float3 in_entryNormal)
{
	TraceResult result;
	result.material = 0;
	result.normal = in_entryNormal;
	result.t = in_tStart;
	result.voxel = int3(0, 0, 0);

	// Nudged off the boundary so floor cannot land on the neighbouring cell
	const float tStart = in_tStart + 0.0001f;
	const float3 entry = in_origin + in_direction * tStart;

	int3 voxel = (int3)floor(entry);
	const int3 stepDirection = (int3)sign(in_direction);
	const float3 tDelta = 1.0f / abs(in_direction);

	const float3 boundary = float3(
		in_direction.x > 0.0f ? voxel.x + 1 : voxel.x,
		in_direction.y > 0.0f ? voxel.y + 1 : voxel.y,
		in_direction.z > 0.0f ? voxel.z + 1 : voxel.z
	);

	float3 tMax = tStart + (boundary - entry) / in_direction;

	for (int i = 0; i < maxSteps; ++i)
	{
		result.material = SampleVoxel(in_model, voxel);

		if (result.material != 0)
		{
			result.voxel = voxel;
			return result;
		}

		// The nearest boundary is the axis to cross, and the axis crossed is the
		// face entered through, so the normal comes out of the step for free
		if (tMax.x < tMax.y && tMax.x < tMax.z)
		{
			if (tMax.x > in_tEnd) { break; }
			result.t = tMax.x;
			voxel.x += stepDirection.x;
			tMax.x += tDelta.x;
			result.normal = float3(-stepDirection.x, 0.0f, 0.0f);
		}
		else if (tMax.y < tMax.z)
		{
			if (tMax.y > in_tEnd) { break; }
			result.t = tMax.y;
			voxel.y += stepDirection.y;
			tMax.y += tDelta.y;
			result.normal = float3(0.0f, -stepDirection.y, 0.0f);
		}
		else
		{
			if (tMax.z > in_tEnd) { break; }
			result.t = tMax.z;
			voxel.z += stepDirection.z;
			tMax.z += tDelta.z;
			result.normal = float3(0.0f, 0.0f, -stepDirection.z);
		}
	}

	result.material = 0;

	return result;
}

struct SceneHit
{
	bool hit;
	float t;
	uint material;
	float3 gridPoint;
	float3 gridNormal;
	int model;
	int instance;
};

// Rotation and translation leave t alone, and so does the uniform scale into voxels, so a
// distance found inside one instance is directly comparable with every other
SceneHit TraceScene(float3 in_origin, float3 in_direction, float in_maxDistance)
{
	SceneHit result;
	result.hit = false;
	result.t = in_maxDistance;
	result.material = 0;
	result.gridPoint = float3(0.0f, 0.0f, 0.0f);
	result.gridNormal = float3(0.0f, 1.0f, 0.0f);
	result.model = 0;
	result.instance = 0;

	const int instanceCount = (int)counts.x;

	for (int i = 0; i < instanceCount; ++i)
	{
		const Instance instance = GetInstance(i);
		const Model model = GetModel(instance.model);

		const float3 localOrigin = WorldToLocal(instance, in_origin - instance.position);
		const float3 localDirection = WorldToLocal(instance, in_direction);

		// Scaling a voxel scales the whole model with it, which is the only thing an instance
		// scale has to mean for a grid of cubes
		const float voxelSize = model.voxelSize * instance.scale;

		// The position places the centre of a model base, so the box is centred on x and z and
		// rises from zero
		const float3 extent = (float3)model.size * voxelSize;
		const float3 boxMin = float3(-extent.x * 0.5f, 0.0f, -extent.z * 0.5f);

		const BoxHit box = IntersectBox(localOrigin, localDirection, boxMin, boxMin + extent);

		// Nothing behind a closer hit can win, so the traversal is skipped entirely
		if (box.hit == false || box.tEnter >= result.t)
		{
			continue;
		}

		const float3 gridOrigin = (localOrigin - boxMin) / voxelSize;
		const float3 gridDirection = localDirection / voxelSize;

		const TraceResult hit = TraceGrid(
			model,
			gridOrigin,
			gridDirection,
			max(box.tEnter, 0.0f),
			min(box.tExit, result.t),
			box.entryNormal
		);

		if (hit.material != 0 && hit.t < result.t)
		{
			result.hit = true;
			result.t = hit.t;
			result.material = hit.material;
			result.gridPoint = gridOrigin + gridDirection * hit.t;
			result.gridNormal = hit.normal;
			result.model = instance.model;
			result.instance = i;
		}
	}

	return result;
}

// Offset off the surface so the ray does not immediately strike the voxel it is leaving. Measured
// in metres rather than in voxels, because the instances a shadow ray crosses may each be a
// different density
static const float shadowBias = 0.002f;
static const float shadowRange = 120.0f;

bool IsInShadow(float3 in_worldPoint, float3 in_worldNormal)
{
	return TraceScene(in_worldPoint + in_worldNormal * shadowBias, keyDirection.xyz, shadowRange).hit;
}

// Biased towards the horizon, where nearly all of a visible sky gradient sits
float3 SkyColour(float3 in_direction)
{
	return lerp(skyHorizon.rgb, skyZenith.rgb, pow(saturate(in_direction.y), 0.45f));
}

// Negative when the ray never reaches the ground plane
float GroundDistance(float3 in_origin, float3 in_direction)
{
	const float distance = -in_origin.y / in_direction.y;

	return (groundLight.w > 0.5f && in_direction.y < 0.0f && distance > 0.0f) ? distance : -1.0f;
}

float3 ShadeGround(float3 in_point, float in_distance)
{
	const float2 tile = in_point.xz / skyZenith.w;
	const float checker = frac((floor(tile.x) + floor(tile.y)) * 0.5f) * 2.0f;

	// At grazing angles a tile shrinks below a pixel and the pattern turns into noise, so it
	// dissolves to the average of the two tones before that happens
	const float tilesPerPixel = in_distance * shadingB.y / skyZenith.w;
	const float visible = saturate(1.0f - tilesPerPixel * 4.0f);

	const float3 albedo = lerp(groundDark.rgb, groundLight.rgb, lerp(0.5f, checker, visible));

	const float3 normal = float3(0.0f, 1.0f, 0.0f);
	float diffuse = saturate(keyDirection.y);

	if (diffuse > 0.0f && IsInShadow(in_point, normal))
	{
		diffuse = 0.0f;
	}

	// Facing straight up, so the hemisphere resolves to the sky end of it
	const float3 ambient = skyColour.rgb * skyColour.w;
	const float3 key = keyColour.rgb * keyDirection.w * diffuse;

	return albedo * (key + ambient);
}

// Towards the sky in the direction the ray was travelling rather than towards one fog colour, so
// distant ground meets the horizon instead of ending at it
float3 ApplyFog(float3 in_colour, float in_distance, float3 in_direction)
{
	return lerp(SkyColour(in_direction), in_colour, exp(-in_distance * skyHorizon.w));
}

float3 ShadeVoxel(SceneHit in_hit, float3 in_worldPoint)
{
	const Model model = GetModel(in_hit.model);
	const Instance instance = GetInstance(in_hit.instance);

	// How much of a voxel one pixel covers here. Derived from the hit distance rather than from
	// screen-space derivatives, which are meaningless across a silhouette where neighbouring
	// pixels are on different surfaces
	const float voxelsPerPixel = in_hit.t * shadingB.y / (model.voxelSize * instance.scale);

	// in_hit.gridNormal stays the geometric normal. Shadow rays are offset along it and the
	// ambient hemisphere is built from it, so it has to be the face the ray actually entered.
	// The shading normal only feeds the light response
	const float3 gridShadingNormal = shadingA.w > 0.5f
		? ShadingNormal(model, in_hit.gridPoint, in_hit.gridNormal, voxelsPerPixel)
		: in_hit.gridNormal;

	const float3 geometricNormal = LocalToWorld(instance, in_hit.gridNormal);
	const float3 shadingNormal = LocalToWorld(instance, gridShadingNormal);

	float diffuse = saturate(dot(shadingNormal, keyDirection.xyz));

	if (diffuse > 0.0f && IsInShadow(in_worldPoint, geometricNormal))
	{
		diffuse = 0.0f;
	}

	const float occlusion = VoxelAmbientOcclusion(model, in_hit.gridPoint, in_hit.gridNormal);

	// Sky light arrives from above and ground bounce from below, so the two are the ends of a
	// hemisphere and the normal picks a point along it. This is what stops everything the key
	// misses from being one flat value - surfaces separate by which way they face even in shadow
	const float upness = saturate(shadingNormal.y * 0.5f + 0.5f);

	const float3 sky = skyColour.rgb * skyColour.w;
	const float3 ground = groundColour.rgb * groundColour.w;

	// Occlusion is how much of the hemisphere a point can see, so it belongs on these two and
	// not on the key, which already has a real shadow ray of its own
	const float3 ambient = lerp(ground, sky, upness) * lerp(ambientSettings.x, 1.0f, occlusion);

	const float3 key = keyColour.rgb * keyDirection.w * diffuse;

	const float3 lit = paletteColours[in_hit.material].rgb * (key + ambient);

	// Driven by the geometric normal so smoothing cannot remove it
	return lit * lerp(1.0f, AxisTint(geometricNormal), shadingA.z);
}

float4 main(PSInput in_input) : SV_Target
{
	float2 ndc;
	ndc.x = in_input.uv.x * 2.0f - 1.0f;
	ndc.y = 1.0f - in_input.uv.y * 2.0f;

	const float scale = positionAndScale.w;
	const float aspect = rightAndAspect.w;

	const float3 origin = positionAndScale.xyz;
	const float3 direction = normalize(
		forward.xyz
		+ ndc.x * scale * aspect * rightAndAspect.xyz
		+ ndc.y * scale * up.xyz
	);

	// Nothing hit yet, so the sky stands and there is no distance to fog over
	float3 colour = SkyColour(direction);
	float distance = -1.0f;

	const float groundDistance = GroundDistance(origin, direction);

	// Passing the ground distance in as the limit lets an instance hidden behind the ground be
	// rejected by the box test rather than traversed and then thrown away
	const SceneHit hit = TraceScene(origin, direction, groundDistance > 0.0f ? groundDistance : shadowRange);

	if (hit.hit)
	{
		colour = ShadeVoxel(hit, origin + direction * hit.t);
		distance = hit.t;
	}
	else if (groundDistance > 0.0f)
	{
		colour = ShadeGround(origin + direction * groundDistance, groundDistance);
		distance = groundDistance;
	}

	if (distance > 0.0f)
	{
		colour = ApplyFog(colour, distance, direction);
	}

	// Linear and unclamped. The present pass exposes, tonemaps and encodes it
	return float4(colour, 1.0f);
}

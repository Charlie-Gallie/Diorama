#include "SceneShared.hlsli"

cbuffer CameraData : register(b0, space3)
{
	float4 positionAndScale; // xyz camera position, w tan(fovY/2)
	float4 rightAndAspect; // xyz camera right, w width/height
	float4 up;
	float4 forward;
	float4 shadingA; // x normal radius, y normal fade, z axis tint, w smooth normals on
	float4 shadingB; // x concavity guard, y pixel angle at unit distance, z shadows on, w occlusion on
};

cbuffer PaletteData : register(b1, space3)
{
	float4 paletteColours[256];
};

// Lights and atmosphere share one buffer because SDL_GPU allows only four per stage
cbuffer EnvironmentData : register(b2, space3)
{
	float4 keyDirection; // xyz unit direction towards the key, w intensity
	float4 keyColour; // xyz linear colour, w terminator wrap
	float4 skyColour; // xyz linear colour, w intensity
	float4 groundColour; // xyz linear colour, w intensity
	float4 ambientSettings; // x occlusion floor, y shadow range, z occlusion bounce, w shadow skip

	float4 skyHorizon; // xyz linear colour, w fog density
	float4 skyZenith; // xyz linear colour, w ground tile size in metres
	float4 groundLight; // xyz linear colour, w ground plane on
	float4 groundDark; // xyz linear colour
};

cbuffer SceneData : register(b3, space3)
{
	SceneConstants scene;
};

Texture3D<uint> voxels : register(t0, space2);

// One texel per cell of voxels, non-zero if anything in that cell is solid. Lets a ray
// cross empty space a cell at a time instead of a voxel at a time
Texture3D<uint> coarseVoxels : register(t1, space2);

// SDL_GPU puts a fragment shader's storage buffers after its storage textures in the same set, so
// these follow the two volumes. Sized by the renderer, so nothing here bounds how many there are
StructuredBuffer<ModelData> modelData : register(t2, space2);
StructuredBuffer<InstanceData> instanceData : register(t3, space2);

// Only has to be generous enough for the largest model. The fine bound, maxFineSteps, is per coarse
// cell rather than per ray and lives with the cell size it depends on
static const int maxCoarseSteps = 256;

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

	// Where each axis' near plane was crossed. Kept rather than turned into a normal here,
	// because most boxes are rejected straight after this and would never use it
	float3 nearPlanes;
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
	result.nearPlanes = tNear;

	return result;
}

// The axis whose near plane produced tEnter is the face the ray entered through, which is the
// normal for a voxel that is already solid before the walk takes a step. Worked out only once a
// box has survived every rejection, rather than for every box that is tested
float3 BoxEntryNormal(BoxHit in_box, float3 in_direction)
{
	return -sign(in_direction) * step(in_box.tEnter.xxx, in_box.nearPlanes);
}

// Every model lives in one 3D texture at its own offset, so a lookup is a local voxel plus that
// offset. The size is the model's own, which is what keeps one model from reading into the next
struct Model
{
	int3 offset;
	int3 size;
	float voxelSize;

	int3 coarseOffset;
	int3 coarseSize;
	float coarseCell;
};

Model GetModel(int in_index)
{
	Model model;
	const ModelData data = modelData[in_index];

	model.offset = (int3)data.offset.xyz;
	model.size = (int3)data.size.xyz;
	model.voxelSize = data.offset.w;
	model.coarseOffset = (int3)data.coarse.xyz;
	model.coarseCell = data.coarse.w;

	// Derived rather than passed, so there is one less number to keep in step with the renderer
	const int cell = (int)model.coarseCell;
	model.coarseSize = (model.size + cell - 1) / cell;

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
	const InstanceData data = instanceData[in_index];

	instance.position = data.transform.xyz;
	instance.model = (int)data.transform.w;
	instance.sinYaw = data.rotation.x;
	instance.cosYaw = data.rotation.y;
	instance.scale = data.rotation.z;

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

uint SampleCoarse(Model in_model, int3 in_cell)
{
	if (any(in_cell < 0) || any(in_cell >= in_model.coarseSize))
	{
		return 0;
	}

	return coarseVoxels.Load(int4(in_model.coarseOffset + in_cell, 0));
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

// How much of the hemisphere a point can see, and the colour of whatever is blocking the rest.
// Light arriving in a crevice got there by bouncing off its walls, so it carries their colour -
// one bounce of it, gathered for free from samples that were being taken anyway
struct Occlusion
{
	float visibility;
	float3 occluderColour;
};

Occlusion VoxelAmbientOcclusion(Model in_model, float3 in_hitPoint, float3 in_normal)
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

	float3 blocked = float3(0.0f, 0.0f, 0.0f);
	float blockedWeight = 0.0f;

	for (int d = 0; d < aoDirections; ++d)
	{
		for (int s = 0; s < aoSamples; ++s)
		{
			// Samples bunch up near the surface, where occluders matter most
			const float distance = aoRadius * pow((s + 1.0f) / aoSamples, 2.0f);
			const int3 sampleVoxel = (int3)floor(in_hitPoint + in_normal * 0.5f + directions[d] * distance);

			const uint material = SampleVoxel(in_model, sampleVoxel);

			if (material != 0)
			{
				// Nearer occluders darken more, and colour more, which is the same weight
				const float weight = 1.0f - distance / aoRadius;

				occlusion += weight;

				blocked += paletteColours[material].rgb * weight;
				blockedWeight += weight;
			}
		}
	}

	const float maximum = aoDirections * aoSamples * 0.5f;

	Occlusion result;
	result.visibility = saturate(1.0f - occlusion / maximum);

	// White where nothing was in the way, so an unoccluded point is left exactly as it was
	result.occluderColour = blockedWeight > 0.0f
		? blocked / blockedWeight
		: float3(1.0f, 1.0f, 1.0f);

	return result;
}

struct TraceResult
{
	uint material;
	float3 normal;
	float t;
	int3 voxel;
};

// Amanatides and Woo. The cells are whatever unit the caller hands it, which is what lets the
// same walk serve both the coarse grid and the voxels inside it
struct Walk
{
	int3 cell;
	int3 step;
	float3 tMax;
	float3 tDelta;
	float t;
	float3 normal;
};

// in_tDelta is how far along the ray one whole cell is, per axis. It belongs to the ray rather
// than to the cell a walk starts in, so the caller works it out once and hands it over - the fine
// walk is restarted in every occupied cell and used to redo those three divides each time
Walk BeginWalk(float3 in_origin, float3 in_direction, float3 in_tDelta, float in_tStart, float3 in_entryNormal)
{
	// Nudged off the boundary so floor cannot land on the neighbouring cell
	const float start = in_tStart + 0.0001f;
	const float3 entry = in_origin + in_direction * start;

	Walk walk;
	walk.cell = (int3)floor(entry);
	walk.step = (int3)sign(in_direction);
	walk.tDelta = in_tDelta;
	walk.t = in_tStart;
	walk.normal = in_entryNormal;

	const float3 boundary = float3(
		in_direction.x > 0.0f ? walk.cell.x + 1 : walk.cell.x,
		in_direction.y > 0.0f ? walk.cell.y + 1 : walk.cell.y,
		in_direction.z > 0.0f ? walk.cell.z + 1 : walk.cell.z
	);

	walk.tMax = start + (boundary - entry) / in_direction;

	return walk;
}

// Distance at which the ray leaves the cell it is currently in
float WalkExit(Walk in_walk)
{
	return min(min(in_walk.tMax.x, in_walk.tMax.y), in_walk.tMax.z);
}

// The nearest boundary is the axis to cross, and the axis crossed is the face entered through,
// so the normal comes out of the step for free
void Advance(inout Walk io_walk)
{
	if (io_walk.tMax.x < io_walk.tMax.y && io_walk.tMax.x < io_walk.tMax.z)
	{
		io_walk.t = io_walk.tMax.x;
		io_walk.cell.x += io_walk.step.x;
		io_walk.tMax.x += io_walk.tDelta.x;
		io_walk.normal = float3(-io_walk.step.x, 0.0f, 0.0f);
	}
	else if (io_walk.tMax.y < io_walk.tMax.z)
	{
		io_walk.t = io_walk.tMax.y;
		io_walk.cell.y += io_walk.step.y;
		io_walk.tMax.y += io_walk.tDelta.y;
		io_walk.normal = float3(0.0f, -io_walk.step.y, 0.0f);
	}
	else
	{
		io_walk.t = io_walk.tMax.z;
		io_walk.cell.z += io_walk.step.z;
		io_walk.tMax.z += io_walk.tDelta.z;
		io_walk.normal = float3(0.0f, 0.0f, -io_walk.step.z);
	}
}

// Two levels: walk the coarse grid, and only step voxel by voxel inside a cell that holds
// something. Air is what a voxel ray spends nearly all its time in, so skipping it a cell at a
// time is most of the cost of a trace.
//
// Both levels are the same ray, so t carries between them untouched - the coarse grid is just
// this one divided through by the cell size
TraceResult TraceGrid(Model in_model, float3 in_origin, float3 in_direction, float in_tStart, float in_tEnd, float3 in_entryNormal)
{
	TraceResult result;
	result.material = 0;
	result.normal = in_entryNormal;
	result.t = in_tStart;
	result.voxel = int3(0, 0, 0);

	// One reciprocal rather than six divides, and the coarse grid is just the fine one scaled, so
	// its step length per axis is the fine one multiplied by the cell size
	const float inverseCell = 1.0f / in_model.coarseCell;

	const float3 coarseOrigin = in_origin * inverseCell;
	const float3 coarseDirection = in_direction * inverseCell;

	const float3 fineDelta = 1.0f / abs(in_direction);
	const float3 coarseDelta = fineDelta * in_model.coarseCell;

	Walk coarse = BeginWalk(coarseOrigin, coarseDirection, coarseDelta, in_tStart, in_entryNormal);

	for (int cellStep = 0; cellStep < maxCoarseSteps; ++cellStep)
	{
		if (coarse.t > in_tEnd)
		{
			break;
		}

		const float cellExit = WalkExit(coarse);

		if (SampleCoarse(in_model, coarse.cell) != 0)
		{
			const float fineEnd = min(cellExit, in_tEnd);

			Walk fine = BeginWalk(in_origin, in_direction, fineDelta, max(coarse.t, in_tStart), coarse.normal);

			for (int voxelStep = 0; voxelStep < maxFineSteps; ++voxelStep)
			{
				if (fine.t > fineEnd)
				{
					break;
				}

				result.material = SampleVoxel(in_model, fine.cell);

				if (result.material != 0)
				{
					result.t = fine.t;
					result.normal = fine.normal;
					result.voxel = fine.cell;

					return result;
				}

				Advance(fine);
			}
		}

		Advance(coarse);
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

// A sphere around an instance's box, used to turn a ray away before it pays for the rotation
// into local space and the three divides inside IntersectBox. The sphere encloses the box, so a
// miss here is a guaranteed miss there and nothing real is ever culled.
//
// in_direction must be unit length, which both callers satisfy - the camera ray is normalised
// and the key direction is normalised on the CPU
bool MissesInstanceSphere(int in_index, float3 in_origin, float3 in_direction, float in_bestT)
{
	const float4 sphere = instanceData[in_index].sphere;
	const float3 toCentre = sphere.xyz - in_origin;
	const float radius = sphere.w;

	const float along = dot(toCentre, in_direction);

	// The ray cannot meet anything inside this sphere before it reaches along - radius, so if
	// that is already past the nearest hit found so far there is nothing here worth looking at.
	// This is what the nearest-first instance order exists to feed: once the closest instance has
	// been hit, everything behind it leaves by this line
	if (along - radius > in_bestT)
	{
		return true;
	}

	// Where the ray comes nearest the centre, clamped to zero so a sphere behind the origin is
	// measured from the origin rather than from a point behind it
	const float3 nearest = toCentre - in_direction * max(along, 0.0f);

	return dot(nearest, nearest) > radius * radius;
}

// Rotation and translation leave t alone, and so does the uniform scale into voxels, so a
// distance found inside one instance is directly comparable with every other
//
// in_anyHit stops at the first instance that blocks the ray rather than going on to find the
// nearest one. A shadow ray only asks whether anything is in the way, so there is nothing to
// gain from testing the instances behind an occluder it has already found. Only .hit is
// meaningful in that mode - .t is wherever the search happened to stop
SceneHit TraceScene(float3 in_origin, float3 in_direction, float in_maxDistance, bool in_anyHit)
{
	SceneHit result;
	result.hit = false;
	result.t = in_maxDistance;
	result.material = 0;
	result.gridPoint = float3(0.0f, 0.0f, 0.0f);
	result.gridNormal = float3(0.0f, 1.0f, 0.0f);
	result.model = 0;
	result.instance = 0;

	const int instanceCount = (int)scene.counts.x;

	for (int i = 0; i < instanceCount; ++i)
	{
		// Before anything else, because it needs no instance data beyond its own row and most
		// instances are nowhere near any given ray
		if (MissesInstanceSphere(i, in_origin, in_direction, result.t))
		{
			continue;
		}

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
			BoxEntryNormal(box, localDirection)
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

			// Folded away at compile time, because HLSL inlines and both call sites pass a
			// literal - so the camera ray keeps the full loop and the shadow ray keeps none of it
			if (in_anyHit)
			{
				return result;
			}
		}
	}

	return result;
}

// Offset off the surface so the ray does not immediately strike the voxel it is leaving. Measured
// in metres rather than in voxels, because the instances a shadow ray crosses may each be a
// different density
static const float shadowBias = 0.002f;

// How far a camera ray looks. It used to borrow the shadow range, which is now a panel setting -
// and at 120 m the near orthographic preset, sitting about 86 m back, had only a few metres of
// margin before the far side of the scene silently vanished. Generous costs almost nothing here,
// because an instance beyond the view is turned away by its sphere and empty space is never walked
static const float viewRange = 1000.0f;

bool IsInShadow(float3 in_worldPoint, float3 in_worldNormal)
{
	// Started a little way along the ray rather than at the surface, so anything smaller than
	// the skip cannot shadow itself. One ray is a yes or a no, so a feature whose shadow is
	// narrower than a pixel does not shade that pixel a little - it removes the key from it
	// outright. Skipping past keeps the shadows worth having and drops the ones that only
	// stipple the ground
	const float skip = ambientSettings.w;

	const float3 start = in_worldPoint + in_worldNormal * shadowBias + keyDirection.xyz * skip;

	// Range comes from the panel rather than a constant here, because trading shadow distance
	// against frame time is a judgement made by eye. The skip comes out of it so the reach is
	// measured from the surface either way
	return TraceScene(start, keyDirection.xyz, max(ambientSettings.y - skip, 0.0f), true).hit;
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

	if (diffuse > 0.0f && shadingB.z > 0.5f && IsInShadow(in_point, normal))
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

	// Wrapped rather than plain Lambert. A one voxel step turns the shading normal a long way
	// over a voxel and a half, and dot alone falls off a cliff there - which is what makes small
	// height changes in grass read as cuts. Standing in for light that scattered rather than
	// arriving straight, which is most of why real foliage has no hard terminator
	const float wrap = keyColour.w;
	float diffuse = saturate((dot(shadingNormal, keyDirection.xyz) + wrap) / (1.0f + wrap));

	if (diffuse > 0.0f && shadingB.z > 0.5f && IsInShadow(in_worldPoint, geometricNormal))
	{
		diffuse = 0.0f;
	}

	Occlusion occlusion;
	occlusion.visibility = 1.0f;
	occlusion.occluderColour = float3(1.0f, 1.0f, 1.0f);

	if (shadingB.w > 0.5f)
	{
		occlusion = VoxelAmbientOcclusion(model, in_hit.gridPoint, in_hit.gridNormal);
	}

	// Sky light arrives from above and ground bounce from below, so the two are the ends of a
	// hemisphere and the normal picks a point along it. This is what stops everything the key
	// misses from being one flat value - surfaces separate by which way they face even in shadow
	const float upness = saturate(shadingNormal.y * 0.5f + 0.5f);

	const float3 sky = skyColour.rgb * skyColour.w;
	const float3 ground = groundColour.rgb * groundColour.w;

	// What an occluded point still receives, per channel. Light in a crevice got there by
	// bouncing off the walls, so it is the occluder's own colour that arrives - a hollow in
	// grass fills with green light rather than fading towards black.
	//
	// The albedo is scaled because one bounce undercounts what really happens in a crevice, and
	// the floor is a minimum rather than the value itself, so a dark occluder can only ever
	// leave it where it already was. At a bounce of zero this collapses to the flat floor it
	// used to be, which is the A/B
	const float3 occludedReach = max(
		occlusion.occluderColour * ambientSettings.z,
		ambientSettings.x.xxx
	);

	// Occlusion is how much of the hemisphere a point can see, so it belongs on these two and
	// not on the key, which already has a real shadow ray of its own
	const float3 ambient = lerp(ground, sky, upness)
		* lerp(occludedReach, float3(1.0f, 1.0f, 1.0f), occlusion.visibility);

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
	const SceneHit hit = TraceScene(origin, direction, groundDistance > 0.0f ? groundDistance : viewRange, false);

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

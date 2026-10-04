cbuffer PresentData : register(b0, space3)
{
	float4 sceneSize; // xy scene texture size in texels, z exposure
};

Texture2D sceneTexture : register(t0, space2);
SamplerState sceneSampler : register(s0, space2);

struct PSInput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

// Narkowicz's fit of the ACES curve. It takes linear light of any magnitude and maps it into
// 0..1, rolling the highlights off instead of clipping them, which is what keeps the warm key
// warm where it is brightest rather than flattening it to white.
// Swappable for AgX later - that is a longer curve with less hue shift at the top end
float3 TonemapAces(float3 in_colour)
{
	const float a = 2.51f;
	const float b = 0.03f;
	const float c = 2.43f;
	const float d = 0.59f;
	const float e = 0.14f;

	return saturate((in_colour * (a * in_colour + b)) / (in_colour * (c * in_colour + d) + e));
}

// The scene is lit in linear, but the swapchain is a UNORM format, so the result has to be
// encoded to be displayed. It happens here rather than in the scene pass so the scene target can
// hold unbounded linear light, and here rather than on an sRGB swapchain because ImGui draws its
// own already-encoded colours into the same target and would otherwise be corrected twice
float3 LinearToSrgb(float3 in_colour)
{
	const float3 low = in_colour * 12.92f;
	const float3 high = 1.055f * pow(abs(in_colour), 1.0f / 2.4f) - 0.055f;

	return select(in_colour <= 0.0031308f, low, high);
}

float4 main(PSInput in_input) : SV_Target
{
	// An exact texel fetch rather than a sample: the viewport already scales by a
	// whole number of pixels, so there is nothing to filter between.
	const int2 size = (int2)sceneSize.xy;
	const int2 texel = clamp((int2)floor(in_input.uv * sceneSize.xy), int2(0, 0), size - 1);

	const float3 scene = sceneTexture.Load(int3(texel, 0)).rgb;

	// Tonemapping after the upscale rather than before it is exact, not an approximation, because
	// the fetch above is a nearest texel - every pixel of an upscaled block holds the same value,
	// so a per-pixel curve gives the same answer either side of the scale.
	// That stops being true the moment anything filters between texels here
	const float3 exposed = scene * sceneSize.z;

	return float4(LinearToSrgb(TonemapAces(exposed)), 1.0f);
}

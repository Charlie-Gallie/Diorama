cbuffer PresentData : register(b0, space3)
{
	float4 sceneSize; // xy scene texture size in texels
};

Texture2D sceneTexture : register(t0, space2);
SamplerState sceneSampler : register(s0, space2);

struct PSInput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

float4 main(PSInput in_input) : SV_Target
{
	// An exact texel fetch rather than a sample: the viewport already scales by a
	// whole number of pixels, so there is nothing to filter between.
	const int2 size = (int2)sceneSize.xy;
	const int2 texel = clamp((int2)floor(in_input.uv * sceneSize.xy), int2(0, 0), size - 1);

	return float4(sceneTexture.Load(int3(texel, 0)).rgb, 1.0f);
}

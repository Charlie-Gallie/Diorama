cbuffer CameraData : register(b0, space3)
{
	float4 positionAndScale; // xyz camera position, w tan(fovY/2)
	float4 rightAndAspect; // xyz camera right, w width/height
	float4 up;
	float4 forward;
};

struct PSInput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

float4 main(PSInput in_input) : SV_Target
{
	float2 ndc;
	ndc.x = in_input.uv.x * 2.0f - 1.0f;
	ndc.y = 1.0f - in_input.uv.y * 2.0f;

	const float scale = positionAndScale.w;
	const float aspect = rightAndAspect.w;
	
	const float3 direction = normalize(
		forward.xyz
		+ ndc.x * scale * aspect * rightAndAspect.xyz
		+ ndc.y * scale * up.xyz
	);
	
	return float4(direction * 0.5f + 0.5f, 1.0f);
}

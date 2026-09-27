struct VSOutput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

VSOutput main(uint in_vertexID : SV_Position)
{
	VSOutput output;

	output.uv = float2((in_vertexID << 1) & 2, in_vertexID & 2);
	output.position = float4(output.uv * float2(2.0f, 2.0f) + float2(-1.0f, 1.0f), 0.0f, 0.0f);

	return output;
}
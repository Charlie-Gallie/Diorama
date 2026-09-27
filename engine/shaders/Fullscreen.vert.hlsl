struct VSOutput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

// ID 0 -> uv (0,0) -> clip (-1, 1)
// ID 1 -> uv (2,0) -> clip (3, 1)
// ID 2 -> uv (0,2) -> clip (-1,-3)
VSOutput main(uint in_vertexID : SV_VertexID)
{
	VSOutput output;

	output.uv = float2((in_vertexID << 1) & 2, in_vertexID & 2);

	// y is negated so uv.y = 0 is the top of the screen
	// w must be 1: if w = 0 all verticies are infinity
	output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);

	return output;
}

struct PSInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput in_input) : SV_Target
{
    return float4(in_input.uv, 0.0f, 1.0f);
}
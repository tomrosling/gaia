cbuffer Constants : register(b0)
{
    float3 CamPos;
};

struct PixelShaderInput
{
    float4 col : COLOUR;
};

float4 main(PixelShaderInput IN) : SV_Target
{
    return IN.col;
}

cbuffer PSSharedConstants : register(b1)
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

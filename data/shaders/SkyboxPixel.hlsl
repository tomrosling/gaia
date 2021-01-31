cbuffer PSSharedConstants : register(b1)
{
    float3 CamPos;
};

TextureCube CubemapTex : register(t0);
SamplerState StaticSampler : register(s0);

struct PixelShaderInput
{
    float3 uvw : POSITION;
};

float4 main(PixelShaderInput IN) : SV_Target
{
    float3 col = CubemapTex.Sample(StaticSampler, IN.uvw).rgb;
    return float4(col, 1.0);
}

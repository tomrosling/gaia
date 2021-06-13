cbuffer PSSharedConstants : register(b1)
{
    float3 CamPos;
};

struct VertexShaderOutput
{
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
    float4 col : COLOUR;
};

float4 main(VertexShaderOutput IN) : SV_Target
{
    const float3 LightDir = normalize(float3(0.5, -0.7, 0.3));

    float ndotl = -dot(IN.nrm, LightDir);
    float3 diffuse = ndotl * IN.col.rgb;

    return float4(diffuse, IN.col.a);
}

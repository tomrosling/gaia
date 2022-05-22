cbuffer PSSharedConstants : register(b1)
{
    float3 CamPos;
    float3 SunDirection;
};

struct VertexShaderOutput
{
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
    float4 col : COLOUR;
};

float4 main(VertexShaderOutput IN) : SV_Target
{
    float ndotl = -dot(IN.nrm, SunDirection);
    float3 diffuse = ndotl * IN.col.rgb;

    return float4(diffuse, IN.col.a);
}

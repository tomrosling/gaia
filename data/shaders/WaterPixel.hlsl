cbuffer PSSharedConstants : register(b1)
{
    float3 CamPos;
    float3 SunDirection;
};

struct PixelShaderInput
{
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
    float4 col : COLOUR;
};

float4 main(PixelShaderInput IN) : SV_Target
{
    // Diffuse
    float ndotl = -dot(IN.nrm, SunDirection);
    float3 diffuse = ndotl * IN.col.rgb;

    // Specular: this is probably awful.
    float3 r = reflect(SunDirection, IN.nrm);
    float3 viewDir = normalize(CamPos - IN.worldPos);
    float specular = pow(saturate(dot(r, viewDir)), 256.0);

    return float4(0.9 * diffuse + 0.4 * specular, IN.col.a);
}

cbuffer Constants : register(b0)
{
    float3 CamPos;
};

struct PixelShaderInput
{
    float3 modelpos : POSITION;
    float3 nrm : NORMAL;
    float3 col : COLOUR;
};

float4 main(PixelShaderInput IN) : SV_Target
{
    const float3 LightDir = normalize(float3(0.5, -0.7, 0.3));

    // Diffuse
    float ndotl = -dot(IN.nrm, LightDir);
    float3 diffuse = ndotl * IN.col;
    
    // Specular: this is probably awful.
    float3 r = 2.0 * ndotl * IN.nrm - LightDir;
    float3 viewDir = normalize(CamPos - IN.modelpos);
    float3 h = normalize(viewDir - LightDir);
    float specular = pow(saturate(dot(h, IN.nrm)), 3.0);

    return float4(0.9 * diffuse + 0.1 * specular, 1.0);
}

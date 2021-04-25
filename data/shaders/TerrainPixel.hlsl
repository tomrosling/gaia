cbuffer PSSharedConstants : register(b1)
{
    float3 CamPos;
};

cbuffer TerrainPSConstantBuffer : register(b2)
{
    float2 HighlightPosXZ;
    float2 ClipmapUVOffset;
    float HighlightRadiusSq;
};

Texture2D DiffuseTex0 : register(t0);
Texture2D DiffuseTex1 : register(t1);
SamplerState StaticSampler : register(s0);


struct DomainShaderOutput
{
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
};

float4 main(DomainShaderOutput IN) : SV_Target
{
    const float3 LightDir = normalize(float3(0.5, -0.7, 0.3));

    // Diffuse
    float ndotl = -dot(IN.nrm, LightDir);
    float2 uv = IN.worldPos.xz * 0.15;
    float3 grass = DiffuseTex0.Sample(StaticSampler, uv).rgb;
    float3 rocks = DiffuseTex1.Sample(StaticSampler, uv).rgb;
    float t = 1.0;
    t += 0.5 * IN.worldPos.y;
    t -= 1.5 * IN.nrm.y;
    float3 diffuse = lerp(grass, rocks, saturate(t));
    diffuse *= ndotl;

    // Specular: this is probably awful.
    //float3 r = reflect(LightDir, IN.nrm);
    //float3 viewDir = normalize(CamPos - IN.worldPos);
    //float specular = pow(saturate(dot(r, viewDir)), 256.0);

    // Selection highlight
    float2 highlightOffset = IN.worldPos.xz - HighlightPosXZ;
    float highlightDistSq = dot(highlightOffset, highlightOffset);
    float3 highlight = float3(0.45, 0.45, 0.45) * smoothstep(0.0, 1.0, HighlightRadiusSq - highlightDistSq);

    return float4(0.9 * diffuse /*+ 0.1 * specular*/ + highlight, 1.0);
}

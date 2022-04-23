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
Texture2D NormalTex0 : register(t2);
Texture2D NormalTex1 : register(t3);
SamplerState StaticSampler : register(s0);


struct DomainShaderOutput
{
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
    float3 tangent : TANGENT;
};

float4 main(DomainShaderOutput IN) : SV_Target
{
    const float3 LightDir =  normalize(float3(0.65, -0.5, 0.65)); // World space
    float2 uv = IN.worldPos.xz * 0.15;

    // Blend between grass and rocks detail based on input height and normal.
    float texBlend = 0.5;
    texBlend += 0.25 * IN.worldPos.y;
    texBlend -= 2.0 * IN.nrm.y;
    texBlend = saturate(texBlend);

    // Normalize tangent frame.
    // Bitangent does not need explicitly normalising since N and T are unit length and perpendicular already
    float3 vertexNormal = normalize(IN.nrm);
    float3 tangent = normalize(IN.tangent);
    float3 bitangent = cross(tangent, vertexNormal);
    float3x3 tbn = float3x3(tangent, bitangent, vertexNormal);
    
    // Sample normal maps.
    float3 grassNrm = NormalTex0.Sample(StaticSampler, uv).xyz;
    float3 rocksNrm = NormalTex1.Sample(StaticSampler, uv).xyz;
    float3 detailNrm = lerp(grassNrm, rocksNrm, texBlend);
    detailNrm = 2.0 * detailNrm - 1.0;
    float3 nrm = mul(detailNrm, tbn);
    
    // Calculate diffuse lighting
    float ndotl = max(-dot(nrm, LightDir), 0.0);
    float3 grassDiffuse = DiffuseTex0.Sample(StaticSampler, uv).rgb;
    float3 rocksDiffuse = DiffuseTex1.Sample(StaticSampler, uv).rgb;
    float3 diffuse = lerp(grassDiffuse, rocksDiffuse, texBlend);
    diffuse *= ndotl;

    // Specular: this is probably awful.
    //float3 r = reflect(LightDir, nrm);
    //float3 viewDir = normalize(CamPos - IN.worldPos);
    //float specular = pow(saturate(dot(r, viewDir)), 256.0);

    // Selection highlight
    float2 highlightOffset = IN.worldPos.xz - HighlightPosXZ;
    float highlightDistSq = dot(highlightOffset, highlightOffset);
    float3 highlight = float3(0.45, 0.45, 0.45) * smoothstep(0.0, 1.0, HighlightRadiusSq - highlightDistSq);

    return float4(0.9 * diffuse /*+ 0.1 * specular*/ + highlight, 1.0);
}

cbuffer PSSharedConstants : register(b1)
{
    float3 CamPos;
    float3 SunDirection;
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
Texture2D SunShadowMap : register(t4);
SamplerState StaticSampler : register(s0);


struct DomainShaderOutput
{
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
    float3 tangent : TANGENT;
    float4 shadowPos : SHADOWPOS;
};

float4 main(DomainShaderOutput IN) : SV_Target
{
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
    
    // Calculate ambient and diffuse lighting.
    float ndotl = max(-dot(nrm, SunDirection), 0.0);
    float3 grassAlbedo = DiffuseTex0.Sample(StaticSampler, uv).rgb;
    float3 rocksAlbedo = DiffuseTex1.Sample(StaticSampler, uv).rgb;
    float3 albedo = lerp(grassAlbedo, rocksAlbedo, texBlend);
    float3 ambient = 0.15 * albedo;
    float3 diffuse = 0.85 * ndotl * albedo;

    // Sample shadow map to occlude diffuse light.
    float shadowFactor = 1.0;

    // Transform shadow pos from clip space [-1, 1] to texture space [0, 1] (depth is already [0, 1])
    float3 lightSpaceCoords = (IN.shadowPos.xyz / IN.shadowPos.w);
    lightSpaceCoords.x =  lightSpaceCoords.x * 0.5 + 0.5;
    lightSpaceCoords.y = -lightSpaceCoords.y * 0.5 + 0.5;
    
    if (all(saturate(lightSpaceCoords) == lightSpaceCoords)) // TODO: Use border colour on sampler and remove this?
    {
        // TODO: Use a comparison sampler and SampleCmp instead
        float shadowmapDepth = SunShadowMap.Sample(StaticSampler, lightSpaceCoords.xy).r;
        float pixelShadowDepth = min(lightSpaceCoords.z, 1.0); // Clamp depth to the range of the shadowmap
        float bias = 0.001;
        shadowFactor = pixelShadowDepth < shadowmapDepth + bias ? 1.0 : 0.0;
        diffuse *= shadowFactor;
    }

    // Selection highlight
    float2 highlightOffset = IN.worldPos.xz - HighlightPosXZ;
    float highlightDistSq = dot(highlightOffset, highlightOffset);
    float3 highlight = float3(0.45, 0.45, 0.45) * smoothstep(0.0, 1.0, HighlightRadiusSq - highlightDistSq);

    return float4(ambient + diffuse + highlight, 1.0);
}

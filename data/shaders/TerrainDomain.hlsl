// Defined when building shaders for shadowmap pass
//#define SHADOW_PASS

cbuffer VSSharedConstants : register(b0)
{
    matrix viewMat;
    matrix projMat;
    matrix mvpMat;
    matrix sunShadowMvpMat;
};

cbuffer TerrainPSConstantBuffer : register(b2)
{
    float2 HighlightPosXZ;
    float2 ClipmapUVOffset;
    float HighlightRadiusSq;
};
 
struct HullShaderControlPointOutput
{
    float2 pos : POSITION0;
};
 
struct HullShaderConstantOutput
{
    float EdgeTessFactor[4]     : SV_TessFactor;
    float InsideTessFactor[2]   : SV_InsideTessFactor;
};

static const int NumClipLevels = 8;
static const int HeightmapSize = 256;
static const float InvTextureRes = 1.0 / (float)HeightmapSize;
static const float HalfTexel = 0.5 * InvTextureRes;
Texture2D HeightmapTex[NumClipLevels] : register(t0);
Texture2D NormalMapTex[NumClipLevels] : register(t8);
SamplerState HeightmapSampler : register(s2); 

struct DomainShaderOutput
{
#ifndef SHADOW_PASS
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
    float3 tangent : TANGENT;
    float4 shadowPos : SHADOWPOS;
#endif
    float4 pos : SV_POSITION;
};

float4 SampleSingleLevel(Texture2D maps[], float2 uv, int clipLevel)
{
    // Scale down for the density of this level.
    uv /= (float)(1 << clipLevel);

    // Translate to actual texture coordinates.
    uv += float2(0.5, 0.5);

    return maps[clipLevel].SampleLevel(HeightmapSampler, uv, 0);
}

float4 SampleBlended(Texture2D maps[], float2 uv, int clipLevel, float blendFactor)
{
    float4 s0 = SampleSingleLevel(maps, uv, clipLevel);

    // Smooth out jumps in detail by blending into the lower clip level.
    // TODO: Some more performance testing to see if this branch helps.
    float t = fmod(blendFactor, 1.0);
    if (t < 0.5)
    {
        return s0;
    }
    else
    {
        float4 s1 = SampleSingleLevel(maps, uv, clipLevel + 1);
        t = saturate(2.0 * (t - 0.5));
        return lerp(s0, s1, t);
    }
}

[domain("quad")]
DomainShaderOutput main(HullShaderConstantOutput input, float2 domain : SV_DomainLocation, OutputPatch<HullShaderControlPointOutput, 2> patch)
{
    DomainShaderOutput OUT;

    const float CellSize = 0.05;

    // Bilinearly interpolate within the AABB we were passed.
    float2 pos2D = lerp(patch[0].pos, patch[1].pos, domain);
 
    // Get global heightmap coordinates (relative to the first clip level).
    // TODO: Also shift by another half a texel?
    float2 uv = pos2D * InvTextureRes / CellSize;

    // Translate the UV relative to our offset, i.e. get the UV distance from the current clipmap centre.
    // Then get the maximum absolute coordinate to pick a clipmap level and scale down.
    float2 offsetUV = uv - ClipmapUVOffset;
    float maxCoord = max(abs(offsetUV.x), abs(offsetUV.y));
    float logMaxCoord = log2(4.0 * maxCoord);

    // We need to leave a few texels of border (on each side, hence * 2) for a few reasons:
    // 1. One because the upload is clamped to the nearest integer texel.
    // 2. One to prevent sampling interpolating across the wrap boundary.
    // 3. I have no idea why it needs more than 2 after quite some time of looking, but 3 does the trick. Possibly an off-by-one error somewhere.
    // (4). Should probably add at least one more to prevent thrashing if the camera jitters on a boundary at all.
    // However the blend between levels mostly gets rid of this anyway so it's kinda moot.
    logMaxCoord += 3.0 * 2.0 * InvTextureRes;
    
    // Clamp logMaxCoord to both limit clipLevel and prevent blending with the NumClipLevel'th level.
    logMaxCoord = clamp(logMaxCoord, 0.0, (float)(NumClipLevels - 1));
    int clipLevel = (int)logMaxCoord;

    // Lookup height and normal.
    float height = SampleBlended(HeightmapTex, uv, clipLevel, logMaxCoord).r;
    float3 worldPos = float3(pos2D.x, height, pos2D.y);
    OUT.pos = mul(projMat, mul(viewMat, float4(worldPos, 1.0)));

#ifndef SHADOW_PASS
    OUT.worldPos = worldPos;
    OUT.nrm = SampleBlended(NormalMapTex, uv, clipLevel, logMaxCoord).rgb;

    // Calculate tangent, always in XY plane (assuming normal has nonzero Y component).
    // Approximately, T = X, B = Z, N = Y
    OUT.tangent = cross(OUT.nrm, float3(0.0, 0.0, 1.0));

    OUT.shadowPos = mul(sunShadowMvpMat, float4(worldPos, 1.0));
#endif

    return OUT;
}

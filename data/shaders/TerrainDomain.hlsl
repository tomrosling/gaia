cbuffer VSSharedConstants : register(b0)
{
    matrix viewMat;
    matrix projMat;
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

static const int NumTilesXZ = 2;
Texture2D HeightmapTex[NumTilesXZ * NumTilesXZ] : register(t0);
SamplerState HeightmapSampler : register(s1);
 
struct DomainShaderOutput
{
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
    float4 pos : SV_POSITION;
};

float SampleHeightmap(float2 uv)
{
    // Split the UV into integer and fractional components so we can sample a tile.
    // This has some minor artefacts at tile boundaries where the filtering gets clamped.
    int2 iuv;
    uv = modf(uv, iuv);

    int2 heightmapXZ = clamp(int2(iuv), 0, NumTilesXZ - 1);
    int heightmapIndex = heightmapXZ.y * NumTilesXZ + heightmapXZ.x;
    Texture2D heightmap = HeightmapTex[heightmapIndex];
    return heightmap.SampleLevel(HeightmapSampler, uv, 0).r;
}

[domain("quad")]
DomainShaderOutput main(HullShaderConstantOutput input, float2 domain : SV_DomainLocation, OutputPatch<HullShaderControlPointOutput, 2> patch)
{
    DomainShaderOutput OUT;

    const float CellSize = 0.05;
    float texDim;
    HeightmapTex[0].GetDimensions(texDim, texDim);

    // Bilinearly interpolate within the AABB we were passed.
    float2 pos2D = lerp(patch[0].pos, patch[1].pos, domain);
 
    // Get global heightmap coordinates.
    float2 uv = pos2D / (CellSize * texDim) + float2(0.5, 0.5);

    // Lookup height.
    float height = SampleHeightmap(uv);
    OUT.worldPos = float3(pos2D.x, height, pos2D.y);
 
    OUT.pos = mul(projMat, mul(viewMat, float4(OUT.worldPos, 1.0)));   

    // Calculate normal.
    // TODO: Filtering is creating artefacts here as the tesselation changes.
    float uvStep = 1.0 / texDim;
    float3 up    = float3(0.0,       SampleHeightmap(float2(uv.x, uv.y + uvStep)), CellSize);
    float3 down  = float3(0.0,       SampleHeightmap(float2(uv.x, uv.y - uvStep)), -CellSize);
    float3 left  = float3(-CellSize, SampleHeightmap(float2(uv.x - uvStep, uv.y)), 0.0);
    float3 right = float3(CellSize,  SampleHeightmap(float2(uv.x + uvStep, uv.y)), 0.0);
    OUT.nrm = normalize(cross(up - down, right - left));

    return OUT;
}

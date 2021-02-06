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

// Find the correct heightmap tile and sample from it (with filtering).
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

// Calcs an approximate normal by sampling around the point.
// https://github.com/Traagen/Render-Terrain/blob/master/Render%20Terrain/RenderTerrainTessDS.hlsl /
// http://thedemonthrone.ca/projects/rendering-terrain/rendering-terrain-part-6-adding-camera-controls-and-fixing-normals/
// Because screw factoring out those cross products myself.
float3 CalcNormal(float2 uv, float texDim)
{
    // Sample slightly further than one texel away from the centre to smooth out wibble,
    // but we lose a fair bit of detail. Could experiment with increasing this a bit and 
    // using fewer samples, and it might be better to augment it with detail/normal mapping
    // in the pixel shader.
    const float CellSize = 0.05;
    const float SampleRadius = 1.5;
    float uvStep = SampleRadius / texDim;

    float2 b = uv + float2(0.0,     -uvStep);
	float2 c = uv + float2(uvStep,  -uvStep);
	float2 d = uv + float2(uvStep,  0.0    );
	float2 e = uv + float2(uvStep,  uvStep );
	float2 f = uv + float2(0.0,     uvStep );
	float2 g = uv + float2(-uvStep, uvStep );
	float2 h = uv + float2(-uvStep, 0.0    );
	float2 i = uv + float2(-uvStep, -uvStep);

	float zb = SampleHeightmap(b);
	float zc = SampleHeightmap(c);
	float zd = SampleHeightmap(d);
	float ze = SampleHeightmap(e);
	float zf = SampleHeightmap(f);
	float zg = SampleHeightmap(g);
	float zh = SampleHeightmap(h);
	float zi = SampleHeightmap(i);
    
	float x = zg + 2.0 * zh + zi - zc - 2.0 * zd - ze;
	float y = SampleRadius * CellSize * 8.0f;
	float z = 2.0 * zb + zc + zi - ze - 2.0 * zf - zg;

	return normalize(float3(x, y, z));
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
    OUT.nrm = CalcNormal(uv, texDim);

    return OUT;
}

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

static const int NumClipLevels = 8;
static const float InvTextureRes = 1.0 / 256.0;
Texture2D HeightmapTex[NumClipLevels] : register(t0);
SamplerState HeightmapSampler : register(s1); 

struct DomainShaderOutput
{
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
    float4 pos : SV_POSITION;
};

float SampleHeightmapLevel(float2 uv, int clipLevel)
{
    // Scale down for the density of this level.
    uv /= (float)(1 << clipLevel);

    // Translate to actual texture coordinates.
    const float HalfTexel = 0.5 * InvTextureRes;
    uv += float2(0.5, 0.5);
    uv += float2(HalfTexel, HalfTexel);

    Texture2D heightmap = HeightmapTex[clipLevel];
    return heightmap.SampleLevel(HeightmapSampler, uv, 0).r;
}

// Find the correct heightmap tile and sample from it (with filtering).
float SampleHeightmap(float2 uv)
{
    // Get the maximum absolute UV coordinate to pick a clipmap level and scale down.
    float maxCoord = max(abs(uv.x), abs(uv.y));
    float logMaxCoord = log2(4.0 * maxCoord);
    int clipLevel = (int)max(0.0, logMaxCoord);
    clipLevel = min(NumClipLevels, clipLevel);
    float s0 = SampleHeightmapLevel(uv, clipLevel);

    // Smooth out jumps in detail by blending into the lower clip level.
    // TODO: Some more performance testing to see if this branch helps.
    float t = fmod(logMaxCoord, 1.0);
    if (t < 0.5)
    {
        return s0;
    }
    else
    {
        float s1 = SampleHeightmapLevel(uv, clipLevel + 1);
        t = saturate(2.0 * (t - 0.5));
        return lerp(s0, s1, t);
    }
}

// Calcs an approximate normal by sampling around the point.
// https://github.com/Traagen/Render-Terrain/blob/master/Render%20Terrain/RenderTerrainTessDS.hlsl /
// http://thedemonthrone.ca/projects/rendering-terrain/rendering-terrain-part-6-adding-camera-controls-and-fixing-normals/
// Because screw factoring out those cross products myself.
float3 CalcNormal(float2 uv)
{
    // Sample slightly further than one texel away from the centre to smooth out wibble,
    // but we lose a fair bit of detail. Could experiment with increasing this a bit and 
    // using fewer samples, and it might be better to augment it with detail/normal mapping
    // in the pixel shader.
    const float CellSize = 0.05;
    const float SampleRadius = 1.5;
    float uvStep = SampleRadius * InvTextureRes;

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

    // Bilinearly interpolate within the AABB we were passed.
    float2 pos2D = lerp(patch[0].pos, patch[1].pos, domain);
 
    // Get global heightmap coordinates (relative to the first clip level).
    // TODO: Also shift by another half a texel?
    float2 uv = pos2D * InvTextureRes / CellSize;

    // Lookup height.
    float height = SampleHeightmap(uv);
    OUT.worldPos = float3(pos2D.x, height, pos2D.y);
 
    OUT.pos = mul(projMat, mul(viewMat, float4(OUT.worldPos, 1.0)));   
    OUT.nrm = CalcNormal(uv);

    return OUT;
}

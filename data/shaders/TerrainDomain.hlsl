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

Texture2D HeightmapTex : register(t0);
SamplerState StaticSampler : register(s0);
 
struct DomainShaderOutput
{
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
    float4 pos : SV_POSITION;
};
 
[domain("quad")]
DomainShaderOutput main(HullShaderConstantOutput input, float2 domain : SV_DomainLocation, OutputPatch<HullShaderControlPointOutput, 2> patch)
{
    DomainShaderOutput OUT;    

    const float CellSize = 0.05;
    float texDim;
    HeightmapTex.GetDimensions(texDim, texDim);
 
    // Bilinearly interpolate within the AABB we were passed.
    float2 pos2D = lerp(patch[0].pos, patch[1].pos, domain);

    // Lookup height.
    float2 uv = pos2D / (CellSize * texDim) + float2(0.5, 0.5);
    float height = HeightmapTex.SampleLevel(StaticSampler, uv, 0).r;
    OUT.worldPos = float3(pos2D.x, height, pos2D.y);
 
    OUT.pos = mul(projMat, mul(viewMat, float4(OUT.worldPos, 1.0)));   

    // Calculate normal.
    float uvStep = 1.0 / texDim;
    float3 up    = float3(0.0,       HeightmapTex.SampleLevel(StaticSampler, float2(uv.x, uv.y + uvStep), 0).r, CellSize);
    float3 down  = float3(0.0,       HeightmapTex.SampleLevel(StaticSampler, float2(uv.x, uv.y - uvStep), 0).r, -CellSize);
    float3 left  = float3(-CellSize, HeightmapTex.SampleLevel(StaticSampler, float2(uv.x - uvStep, uv.y), 0).r, 0.0);
    float3 right = float3(CellSize,  HeightmapTex.SampleLevel(StaticSampler, float2(uv.x + uvStep, uv.y), 0).r, 0.0);
    OUT.nrm = normalize(cross(up - down, right - left));

    return OUT;
}

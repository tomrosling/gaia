cbuffer VSSharedConstants : register(b0)
{
    matrix viewMat;
    matrix projMat;
};
 
struct Vertex
{
    float2 pos : POSITION;
};

Texture2D HeightmapTex : register(t0);
SamplerState StaticSampler : register(s0);

struct VertexShaderOutput
{
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
    float4 pos : SV_Position;
};

 
VertexShaderOutput main(Vertex IN)
{
    VertexShaderOutput OUT;

    const float CellSize = 0.05;
    float texDim;
    HeightmapTex.GetDimensions(texDim, texDim);
 
    float2 uv = IN.pos / (CellSize * texDim) + float2(0.5, 0.5);
    float height = HeightmapTex.SampleLevel(StaticSampler, uv, 0).r;
    float3 pos = float3(IN.pos.x, height, IN.pos.y);

    OUT.pos = mul(projMat, mul(viewMat, float4(pos, 1.0)));
    OUT.worldPos = pos;

    float uvStep = 1.0 / texDim;
    float4 patch = HeightmapTex.Gather(StaticSampler, uv);
    float3 up    = float3(0.0,       HeightmapTex.SampleLevel(StaticSampler, float2(uv.x, uv.y + uvStep), 0).r, CellSize);
    float3 down  = float3(0.0,       HeightmapTex.SampleLevel(StaticSampler, float2(uv.x, uv.y - uvStep), 0).r, -CellSize);
    float3 left  = float3(-CellSize, HeightmapTex.SampleLevel(StaticSampler, float2(uv.x - uvStep, uv.y), 0).r, 0.0);
    float3 right = float3(CellSize,  HeightmapTex.SampleLevel(StaticSampler, float2(uv.x + uvStep, uv.y), 0).r, 0.0);
    OUT.nrm = normalize(cross(up - down, right - left));
 
    return OUT;
}

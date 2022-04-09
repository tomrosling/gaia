
struct ComputeShaderInput
{
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
};

cbuffer ComputeNormalsConstants : register(b0)
{
    int2 UVMin;
    float WorldTexelSizeTimes8;
}

static const int TextureDimension = 256; // Hardcoded for efficiency. Keep in sync with TerrainConstants.hpp
Texture2D<float> SrcHeightMap : register(t0);
RWTexture2D<float3> OutNormalMap : register(u0);

#define ComputeNormals_RootSignature                  \
    "RootFlags(0), "                                  \
    "RootConstants(b0, num32BitConstants = 5),"       \
    "DescriptorTable(SRV(t0, numDescriptors = 1)),"   \
    "DescriptorTable(UAV(u0, numDescriptors = 1)),"

[RootSignature(ComputeNormals_RootSignature)]
[numthreads(8, 8, 1)]
void main(ComputeShaderInput IN)
{
	int2 coords = UVMin + IN.DispatchThreadID.xy;

    // TODO: Could this be more efficient by doing 4x4 blocks per thread? (saving loads)
    int2 b = coords + int2(0, -1);
    int2 c = coords + int2(1, -1);
    int2 d = coords + int2(1, 0);
    int2 e = coords + int2(1, 1);
    int2 f = coords + int2(0, 1);
    int2 g = coords + int2(-1, 1);
    int2 h = coords + int2(-1, 0);
    int2 i = coords + int2(-1, -1);
    
    float zb = SrcHeightMap.Load(int3(b & (TextureDimension - 1), 0), 0);
    float zc = SrcHeightMap.Load(int3(c & (TextureDimension - 1), 0), 0);
    float zd = SrcHeightMap.Load(int3(d & (TextureDimension - 1), 0), 0);
    float ze = SrcHeightMap.Load(int3(e & (TextureDimension - 1), 0), 0);
    float zf = SrcHeightMap.Load(int3(f & (TextureDimension - 1), 0), 0);
    float zg = SrcHeightMap.Load(int3(g & (TextureDimension - 1), 0), 0);
    float zh = SrcHeightMap.Load(int3(h & (TextureDimension - 1), 0), 0);
    float zi = SrcHeightMap.Load(int3(i & (TextureDimension - 1), 0), 0);

    float nx = zg + 2.0 * zh + zi - zc - 2.0 * zd - ze;
    float ny = WorldTexelSizeTimes8;
    float nz = 2.0 * zb + zc + zi - ze - 2.0 * zf - zg;

    float3 normal = normalize(float3(nx, ny, nz));
    OutNormalMap[coords & (TextureDimension - 1)] = normal;
}

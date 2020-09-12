#include "Terrain.hpp"
#include "Renderer.hpp"

#define STB_PERLIN_IMPLEMENTATION
#include <stb_perlin.h>

namespace gaia
{

const int NumTilesX = 2;
const int NumTilesZ = 2;
const int CellsPerTileX = 255;
const int CellsPerTileZ = 255;
const int VertsPerTile = (CellsPerTileX + 1) * (CellsPerTileZ + 1);
const int IndicesPerTile = 2 * 3 * CellsPerTileX * CellsPerTileZ;
const float CellSize = 0.05f;
static_assert(VertsPerTile <= (1 << 16), "Index format too small");


static int VertexIndex(int x, int z) 
{
    assert(0 <= x && x <= CellsPerTileX);
    assert(0 <= z && z <= CellsPerTileZ);
    return (CellsPerTileX + 1) * z + x;
}

static int TileIndex(int x, int z)
{
    assert(0 <= x && x <= NumTilesX);
    assert(0 <= z && z <= NumTilesZ);
    return (NumTilesX) * z + x;
}

static Vec2i WorldPosToTile(Vec2f worldPosXZ)
{
    // Account for the world being centred.
    // Probably remove this in future.
    worldPosXZ.x += 0.5f * CellSize * (float)(CellsPerTileX * NumTilesX);
    worldPosXZ.y += 0.5f * CellSize * (float)(CellsPerTileZ * NumTilesZ);

    return Vec2i((int)floorf(worldPosXZ.x / (CellSize * (float)CellsPerTileX)),
                 (int)floorf(worldPosXZ.y / (CellSize * (float)CellsPerTileZ)));
}

static Vec2i WorldPosToCell(Vec2f worldPosXZ, Vec2i tileCoords)
{
    // Account for the world being centred.
    // Probably remove this in future.
    worldPosXZ.x += 0.5f * CellSize * (float)(CellsPerTileX * NumTilesX);
    worldPosXZ.y += 0.5f * CellSize * (float)(CellsPerTileZ * NumTilesZ);

    Vec2i cellPos((int)floorf(worldPosXZ.x / CellSize),
                  (int)floorf(worldPosXZ.y / CellSize));
    cellPos -= Vec2i(tileCoords.x * CellsPerTileX, tileCoords.y * CellsPerTileZ);

    cellPos.x = std::clamp(cellPos.x, 0, CellsPerTileX);
    cellPos.y = std::clamp(cellPos.y, 0, CellsPerTileZ);

    return cellPos;
}


void Terrain::Build(Renderer& renderer)
{
    renderer.BeginUploads();

    // Note: rand() is not seeded so this is still deterministic, for now.
    m_seed = rand();

    m_tileVertexBuffers.resize(NumTilesX * NumTilesZ);
    for (int z = 0; z < NumTilesZ; ++z)
    {
        for (int x = 0; x < NumTilesX; ++x)
        {
            BuildTile(renderer, x, z);
        }
    }

    BuildIndexBuffer(renderer);
    BuildWater(renderer);

    m_uploadFenceVal = renderer.EndUploads();
}

void Terrain::Render(Renderer& renderer)
{
    // Wait for any pending uploads.
    if (m_uploadFenceVal != 0)
    {
        renderer.WaitUploads(m_uploadFenceVal);
        m_uploadFenceVal = 0;
    }

    ID3D12GraphicsCommandList& commandList = renderer.GetDirectCommandList();
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Render the terrain itself.
    for (const VertexBuffer& vertexBuffer : m_tileVertexBuffers)
    {
        commandList.IASetVertexBuffers(0, 1, &vertexBuffer.views[vertexBuffer.currentBuffer]);
        commandList.IASetIndexBuffer(&m_indexBuffer.view);
        commandList.DrawIndexedInstanced(IndicesPerTile, 1, 0, 0, 0);
    }

    // Render "water".
    commandList.IASetVertexBuffers(0, 1, &m_waterVertexBufferView);
    commandList.IASetIndexBuffer(&m_waterIndexBuffer.view);
    commandList.DrawIndexedInstanced(m_waterIndexBuffer.view.SizeInBytes / sizeof(uint16_t), 1, 0, 0, 0);
}

void Terrain::RaiseAreaRounded(Renderer& renderer, Vec2f posXZ, float radius, float raiseBy)
{
    // Check buffers not already being uploaded.
    assert(m_uploadFenceVal == 0);

    // Find all tiles touched by this transform.
    Vec2f minPosXZ = posXZ - Vec2f(radius, radius);
    Vec2f maxPosXZ = posXZ + Vec2f(radius, radius);
    Vec2i minTile = WorldPosToTile(minPosXZ);
    Vec2i maxTile = WorldPosToTile(maxPosXZ);

    // Skip if outside the world.
    if (minTile.x >= NumTilesX || minTile.y >= NumTilesZ)
        return;
    if (maxTile.x < 0 || maxTile.y < 0)
        return;

    minTile.x = std::clamp(minTile.x, 0, NumTilesX - 1);
    minTile.y = std::clamp(minTile.y, 0, NumTilesZ - 1);
    maxTile.x = std::clamp(maxTile.x, 0, NumTilesX - 1);
    maxTile.y = std::clamp(maxTile.y, 0, NumTilesZ - 1);

    renderer.BeginUploads();
    ID3D12GraphicsCommandList& commandList = renderer.GetCopyCommandList();

    for (int tileZ = minTile.y; tileZ <= maxTile.y; ++tileZ)
    {
        for (int tileX = minTile.x; tileX <= maxTile.x; ++tileX)
        {
            VertexBuffer& vb = m_tileVertexBuffers[TileIndex(tileX, tileZ)];

            // Find bounds within the tile that could be touched.
            Vec2i tileCoords(tileX, tileZ);
            Vec2i minVert = WorldPosToCell(minPosXZ, tileCoords);
            Vec2i maxVert = WorldPosToCell(maxPosXZ, tileCoords);

            // Update heightmap.
            for (int z = minVert.y; z <= maxVert.y; ++z)
            {
                for (int x = minVert.x; x <= maxVert.x; ++x)
                {
                    int globalX = x + tileX * CellsPerTileX;
                    int globalZ = z + tileZ * CellsPerTileZ;
                    Vec3f pos = ToVertexPos(globalX, 0.f, globalZ);
                    float distSq = math::length2(Vec2f(pos.x, pos.z) - posXZ);
                    vb.heightmap[VertexIndex(x, z)] += raiseBy * std::max(math::Square(radius) - distSq, 0.f);
                }
            }

            // Map the intermediate buffer.
            Vertex* vertexData = nullptr;
            D3D12_RANGE readRange = { 1, 0 };
            vb.intermediateBuffer->Map(0, &readRange, (void**)&vertexData);
            assert(vertexData);

            // Write to mapped buffer, updating normals and colours as we go.
            for (int z = minVert.y; z <= maxVert.y; ++z)
            {
                for (int x = minVert.x; x <= maxVert.x; ++x)
                {
                    UpdateVertex(vertexData, vb.heightmap, Vec2i(x, z), Vec2i(tileX, tileZ));
                }
            }

            D3D12_RANGE writeRange = {
                VertexIndex(minVert.x, minVert.y) * sizeof(Vertex),
                (VertexIndex(maxVert.x, maxVert.y) + 1) * sizeof(Vertex)
            };
            vb.intermediateBuffer->Unmap(0, &writeRange);

            vb.currentBuffer ^= 1;
            commandList.CopyBufferRegion(vb.gpuDoubleBuffer[vb.currentBuffer].Get(), writeRange.Begin, 
                vb.intermediateBuffer.Get(), writeRange.Begin, writeRange.End - writeRange.Begin);
        }
    }

    m_uploadFenceVal = renderer.EndUploads();
}

float Terrain::Raycast(Vec3f rayStart, Vec3f rayEnd)
{
    // Placeholder - just cast against y == 0.
    if (fabsf(rayStart.y - rayEnd.y) < FLT_EPSILON)
        return -1.f;
    float t = rayStart.y / (rayStart.y - rayEnd.y);
    return (t <= 1.f) ? t : -1.f;
}

void Terrain::BuildIndexBuffer(Renderer& renderer)
{
    size_t dataSize = IndicesPerTile * sizeof(uint16_t);
    m_indexBuffer.buffer = renderer.CreateResidentBuffer(dataSize);
    m_indexBuffer.intermediateBuffer = renderer.CreateUploadBuffer(dataSize);
    m_indexBuffer.view.BufferLocation = m_indexBuffer.buffer->GetGPUVirtualAddress();
    m_indexBuffer.view.Format = DXGI_FORMAT_R16_UINT;
    m_indexBuffer.view.SizeInBytes = (UINT)dataSize;

    uint16_t* indexData = nullptr;
    m_indexBuffer.intermediateBuffer->Map(0, nullptr, (void**)&indexData);
    assert(indexData);

    // TODO: Consider triangle strips or other topology?
    for (int z = 0; z < CellsPerTileZ; ++z)
    {
        for (int x = 0; x < CellsPerTileX; ++x)
        {
            uint16_t* p = &indexData[2 * 3 * (CellsPerTileX * z + x)];
            p[0] = (CellsPerTileX + 1) * (z + 0) + (x + 0);
            p[1] = (CellsPerTileX + 1) * (z + 1) + (x + 0);
            p[2] = (CellsPerTileX + 1) * (z + 1) + (x + 1);
            p[3] = (CellsPerTileX + 1) * (z + 0) + (x + 0);
            p[4] = (CellsPerTileX + 1) * (z + 1) + (x + 1);
            p[5] = (CellsPerTileX + 1) * (z + 0) + (x + 1);
        }
    }

    m_indexBuffer.intermediateBuffer->Unmap(0, nullptr);

    ID3D12GraphicsCommandList& commandList = renderer.GetCopyCommandList();
    commandList.CopyBufferRegion(m_indexBuffer.buffer.Get(), 0, m_indexBuffer.intermediateBuffer.Get(), 0, dataSize);
}

void Terrain::BuildTile(Renderer& renderer, int tileX, int tileZ)
{
    assert(0 <= tileX && tileX < NumTilesX);
    assert(0 <= tileZ && tileZ < NumTilesZ);

    size_t dataSize = VertsPerTile * sizeof(Vertex);
    VertexBuffer& vb = m_tileVertexBuffers[TileIndex(tileX, tileZ)];
    for (int i = 0; i < 2; ++i)
    {
        vb.gpuDoubleBuffer[i] = renderer.CreateResidentBuffer(dataSize);
        vb.views[i].BufferLocation = vb.gpuDoubleBuffer[i]->GetGPUVirtualAddress();
        vb.views[i].SizeInBytes = (UINT)dataSize;
        vb.views[i].StrideInBytes = sizeof(Vertex);
    }
    vb.intermediateBuffer = renderer.CreateUploadBuffer(dataSize);

    // Generate heightmap.
    vb.heightmap.reserve(VertsPerTile);
    for (int z = 0; z <= CellsPerTileZ; ++z)
    {
        for (int x = 0; x <= CellsPerTileX; ++x)
        {
            int globalX = x + tileX * CellsPerTileX;
            int globalZ = z + tileZ * CellsPerTileZ;
            vb.heightmap.push_back(GenerateHeight(globalX, globalZ));
        }
    }

    // Map buffer and fill in vertex data.
    Vertex* vertexData = nullptr;
    vb.intermediateBuffer->Map(0, nullptr, (void**)&vertexData);
    assert(vertexData);

    for (int z = 0; z <= CellsPerTileZ; ++z)
    {
        for (int x = 0; x <= CellsPerTileX; ++x)
        {
            UpdateVertex(vertexData, vb.heightmap, Vec2i(x, z), Vec2i(tileX, tileZ));
        }
    }

    vb.intermediateBuffer->Unmap(0, nullptr);

    // Upload initial data to both buffers.
    ID3D12GraphicsCommandList& commandList = renderer.GetCopyCommandList();
    for (const ComPtr<ID3D12Resource>& buf : vb.gpuDoubleBuffer)
    {
        commandList.CopyBufferRegion(buf.Get(), 0, vb.intermediateBuffer.Get(), 0, dataSize);
    }
}

void Terrain::BuildWater(Renderer& renderer)
{
    const float HalfGridSizeX = 0.5f * CellSize * (float)(CellsPerTileX * NumTilesX);
    const float HalfGridSizeZ = 0.5f * CellSize * (float)(CellsPerTileZ * NumTilesZ);
    const Vertex WaterVerts[] = {
        { { -HalfGridSizeX, 0.f, -HalfGridSizeZ }, Vec3fY, { 0x20, 0x70, 0xff, 0x80 } },
        { { -HalfGridSizeX, 0.f,  HalfGridSizeZ }, Vec3fY, { 0x20, 0x70, 0xff, 0x80 } },
        { {  HalfGridSizeX, 0.f,  HalfGridSizeZ }, Vec3fY, { 0x20, 0x70, 0xff, 0x80 } },
        { {  HalfGridSizeX, 0.f, -HalfGridSizeZ }, Vec3fY, { 0x20, 0x70, 0xff, 0x80 } },
    };

    const uint16_t WaterIndices[] = {
        0, 1, 2,
        0, 2, 3
    };

    renderer.CreateBuffer(m_waterVertexBuffer, m_waterIntermediateVertexBuffer, sizeof(WaterVerts), WaterVerts);
    m_waterVertexBufferView.BufferLocation = m_waterVertexBuffer->GetGPUVirtualAddress();
    m_waterVertexBufferView.SizeInBytes = sizeof(WaterVerts);
    m_waterVertexBufferView.StrideInBytes = sizeof(Vertex);

    renderer.CreateBuffer(m_waterIndexBuffer.buffer, m_waterIndexBuffer.intermediateBuffer, sizeof(WaterIndices), WaterIndices);
    m_waterIndexBuffer.view.BufferLocation = m_waterIndexBuffer.buffer->GetGPUVirtualAddress();
    m_waterIndexBuffer.view.Format = DXGI_FORMAT_R16_UINT;
    m_waterIndexBuffer.view.SizeInBytes = sizeof(WaterIndices);
}

void Terrain::UpdateVertex(Vertex* mappedVertexData, const std::vector<float>& heightmap, Vec2i vertexCoords, Vec2i tileCoords)
{
    int globalX = vertexCoords.x + tileCoords.x * CellsPerTileX;
    int globalZ = vertexCoords.y + tileCoords.y * CellsPerTileZ;
    int index = VertexIndex(vertexCoords.x, vertexCoords.y);

    Vertex& v = mappedVertexData[index];
    float height = heightmap[index];
    v.position = ToVertexPos(globalX, height, globalZ);
    v.normal = GenerateNormal(heightmap, vertexCoords, tileCoords);
    v.colour = GenerateCol(height);
}

float Terrain::GenerateHeight(int globalX, int globalZ)
{
    const struct
    {
        float frequency;
        float amplitude;
    } Octaves[] = {
        { 1.f / 131.f, 3.f },
        { 1.f / 51.f, 1.f },
        { 1.f / 13.f, 0.15f },
        { 0.9999f, 0.05f }
    };

    float height = 0.5f;

    // May be better just to use one of the built in stb_perlin functions
    // to generate multiple octaves, but this is quite nice for now.
    for (int i = 0; i < (int)std::size(Octaves); ++i)
    {
        auto [frequency, amplitude] = Octaves[i];
        height += amplitude * stb_perlin_noise3_seed((float)globalX * frequency, 0.f, (float)globalZ * frequency, 0, 0, 0, m_seed + i);
    }

    return height;
}

Vec3f Terrain::ToVertexPos(int globalX, float height, int globalZ)
{
    return Vec3f(
        CellSize * ((float)globalX - 0.5f * (float)(CellsPerTileX * NumTilesX)),
        height,
        CellSize * ((float)globalZ - 0.5f * (float)(CellsPerTileZ * NumTilesZ)));
}

Vec4u8 Terrain::GenerateCol(float height)
{
    float t = std::clamp(height - 0.8f, 0.f, 1.f);
    Vec4f colf = math::Lerp(Vec4f(0.f, 0.5f, 0.f, 1.f), Vec4f(1.f, 1.f, 1.f, 1.f), t);
    return (Vec4u8)(colf * 255.f);
}

Vec3f Terrain::GenerateNormal(const std::vector<float>& heightmap, Vec2i vertexCoords, Vec2i tileCoords)
{
    int x = vertexCoords.x;
    int z = vertexCoords.y;
    int globalX = x + tileCoords.x * CellsPerTileX;
    int globalZ = z + tileCoords.y * CellsPerTileZ;

    // Make sure seams have consistent normals by resampling the Perlin noise when the neighbouring vertices don't exist.
    // We could avoid this by adding an unrendered border to each tile, and just sampling its height.
    // Could also probably do this on the GPU.
    // TODO: This is no longer valid now the terrain can be modified.
    float leftHeight = (x > 0) ? heightmap[VertexIndex(x - 1, z)] : GenerateHeight(globalX - 1, globalZ);
    float downHeight = (z > 0) ? heightmap[VertexIndex(x, z - 1)] : GenerateHeight(globalX, globalZ - 1);
    float rightHeight = (x < CellsPerTileX) ? heightmap[VertexIndex(x + 1, z)] : GenerateHeight(globalX + 1, globalZ);
    float upHeight = (z < CellsPerTileZ) ? heightmap[VertexIndex(x, z + 1)] : GenerateHeight(globalX, globalZ + 1);

    Vec3f left = ToVertexPos(globalX - 1, leftHeight, globalZ);
    Vec3f down = ToVertexPos(globalX, downHeight, globalZ - 1);
    Vec3f right = ToVertexPos(globalX + 1, rightHeight, globalZ);
    Vec3f up = ToVertexPos(globalX, upHeight, globalZ + 1);

    return math::normalize(math::cross(up - down, right - left));
}

}

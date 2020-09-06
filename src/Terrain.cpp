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


void Terrain::Build(Renderer& renderer)
{
    renderer.BeginUploads();

    // Note: rand() is not seeded so this is still deterministic, for now.
    int seed = rand();

    m_tileVertexBuffers.resize(NumTilesX * NumTilesZ);
    for (int z = 0; z < NumTilesZ; ++z)
    {
        for (int x = 0; x < NumTilesX; ++x)
        {
            BuildTile(renderer, x, z, seed);
        }
    }

    BuildIndexBuffer(renderer);
    BuildWater(renderer);

    renderer.EndUploads();
}

void Terrain::Render(Renderer& renderer)
{
    ID3D12GraphicsCommandList& commandList = renderer.GetDirectCommandList();
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Render the terrain itself.
    for (const VertexBuffer& vertexBuffer : m_tileVertexBuffers)
    {
        commandList.IASetVertexBuffers(0, 1, &vertexBuffer.view);
        commandList.IASetIndexBuffer(&m_indexBuffer.view);
        commandList.DrawIndexedInstanced(IndicesPerTile, 1, 0, 0, 0);
    }

    // Render "water".
    commandList.IASetVertexBuffers(0, 1, &m_waterVertexBuffer.view);
    commandList.IASetIndexBuffer(&m_waterIndexBuffer.view);
    commandList.DrawIndexedInstanced(m_waterIndexBuffer.view.SizeInBytes / sizeof(uint16_t), 1, 0, 0, 0);
}

void Terrain::RaiseAreaRounded(Renderer& renderer, Vec2f posXZ, float radius, float raiseBy)
{
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

    minTile.x = std::clamp(minTile.x, 0, NumTilesX);
    minTile.y = std::clamp(minTile.y, 0, NumTilesZ);
    maxTile.x = std::clamp(maxTile.x, 0, NumTilesX);
    maxTile.y = std::clamp(maxTile.y, 0, NumTilesZ);

    // TODO: Remove this wait by double buffering while uploading.
    renderer.WaitCurrentFrame();
    renderer.BeginUploads();
    ID3D12GraphicsCommandList& commandList = renderer.GetCopyCommandList();

    for (int tileZ = minTile.y; tileZ <= maxTile.y; ++tileZ)
    {
        for (int tileX = minTile.x; tileX <= maxTile.x; ++tileX)
        {
            VertexBuffer& vb = m_tileVertexBuffers[TileIndex(tileX, tileZ)];
            Vertex* vertexData = nullptr;
            vb.intermediateBuffer->Map(0, nullptr, (void**)&vertexData);
            assert(vertexData);

            // TODO: Only iterate the vertices that could be touched.
            for (int z = 0; z <= CellsPerTileZ; ++z)
            {
                for (int x = 0; x <= CellsPerTileX; ++x)
                {
                    Vertex& v = vertexData[VertexIndex(x, z)];
                    float distSq = math::length2(Vec2f(v.position.x, v.position.z) - posXZ);
                    v.position.y += raiseBy * std::max(math::Square(radius) - distSq, 0.f);
                }
            }

            // TODO: Update normals and colours

            vb.intermediateBuffer->Unmap(0, nullptr);
            commandList.CopyBufferRegion(vb.buffer.Get(), 0, vb.intermediateBuffer.Get(), 0, VertsPerTile * sizeof(Vertex));
        }
    }

    renderer.EndUploads();
}

void Terrain::BuildIndexBuffer(Renderer& renderer)
{
    size_t dataSize = IndicesPerTile * sizeof(uint16_t);
    renderer.CreateBuffer(m_indexBuffer.buffer, m_indexBuffer.intermediateBuffer, dataSize);
    m_indexBuffer.view.BufferLocation = m_indexBuffer.buffer->GetGPUVirtualAddress();
    m_indexBuffer.view.Format = DXGI_FORMAT_R16_UINT;
    m_indexBuffer.view.SizeInBytes = (UINT)dataSize;

    uint16_t* indexData = nullptr;
    m_indexBuffer.intermediateBuffer->Map(0, nullptr, (void**)&indexData);

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

void Terrain::BuildTile(Renderer& renderer, int tileX, int tileZ, int seed)
{
    assert(0 <= tileX && tileX < NumTilesX);
    assert(0 <= tileZ && tileZ < NumTilesZ);

    size_t dataSize = VertsPerTile * sizeof(Vertex);
    VertexBuffer& vb = m_tileVertexBuffers[TileIndex(tileX, tileZ)];
    renderer.CreateBuffer(vb.buffer, vb.intermediateBuffer, dataSize);
    vb.view.BufferLocation = vb.buffer->GetGPUVirtualAddress();
    vb.view.SizeInBytes = (UINT)dataSize;
    vb.view.StrideInBytes = sizeof(Vertex);
    
    Vertex* vertexData = nullptr;
    vb.intermediateBuffer->Map(0, nullptr, (void**)&vertexData);

    // Initialise pos, col / generate heights
    for (int z = 0; z <= CellsPerTileZ; ++z)
    {
        for (int x = 0; x <= CellsPerTileX; ++x)
        {
            int globalX = x + tileX * CellsPerTileX;
            int globalZ = z + tileZ * CellsPerTileZ;

            Vertex& v = vertexData[VertexIndex(x, z)];
            v.position = GeneratePos(globalX, globalZ, seed);

            // Leave v.normal uninitialised until below...

            float t = std::clamp(v.position.y - 0.8f, 0.f, 1.f);
            v.colour[0] = math::Lerp(0x00, 0xff, t);
            v.colour[1] = math::Lerp(0x80, 0xff, t);
            v.colour[2] = math::Lerp(0x00, 0xff, t);
            v.colour[3] = 0xff;
        }
    }

    // Generate normals
    for (int z = 0; z <= CellsPerTileZ; ++z)
    {
        for (int x = 0; x <= CellsPerTileX; ++x)
        {
            Vertex& v = vertexData[VertexIndex(x, z)];
            int globalX = x + tileX * CellsPerTileX;
            int globalZ = z + tileZ * CellsPerTileZ;

            // Make sure seams have consistent normals by resampling the Perlin noise when the neighbouring vertices don't exist.
            // We could avoid this by adding an unrendered border to each tile, and just sampling its height.
            // Could also probably do this on the GPU.
            Vec3f left = (x > 0) ? vertexData[VertexIndex(x - 1, z)].position : GeneratePos(globalX - 1, globalZ, seed);
            Vec3f down = (z > 0) ? vertexData[VertexIndex(x, z - 1)].position : GeneratePos(globalX, globalZ - 1, seed);
            Vec3f right = (x < CellsPerTileX) ? vertexData[VertexIndex(x + 1, z)].position : GeneratePos(globalX + 1, globalZ, seed);
            Vec3f up    = (z < CellsPerTileZ) ? vertexData[VertexIndex(x, z + 1)].position : GeneratePos(globalX, globalZ + 1, seed);

            v.normal = math::normalize(math::cross(up - down, right - left));
        }
    }

    vb.intermediateBuffer->Unmap(0, nullptr);

    ID3D12GraphicsCommandList& commandList = renderer.GetCopyCommandList();
    commandList.CopyBufferRegion(vb.buffer.Get(), 0, vb.intermediateBuffer.Get(), 0, dataSize);
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

    renderer.CreateBuffer(m_waterVertexBuffer.buffer, m_waterVertexBuffer.intermediateBuffer, sizeof(WaterVerts), WaterVerts);
    m_waterVertexBuffer.view.BufferLocation = m_waterVertexBuffer.buffer->GetGPUVirtualAddress();
    m_waterVertexBuffer.view.SizeInBytes = sizeof(WaterVerts);
    m_waterVertexBuffer.view.StrideInBytes = sizeof(Vertex);

    renderer.CreateBuffer(m_waterIndexBuffer.buffer, m_waterIndexBuffer.intermediateBuffer, sizeof(WaterIndices), WaterIndices);
    m_waterIndexBuffer.view.BufferLocation = m_waterIndexBuffer.buffer->GetGPUVirtualAddress();
    m_waterIndexBuffer.view.Format = DXGI_FORMAT_R16_UINT;
    m_waterIndexBuffer.view.SizeInBytes = sizeof(WaterIndices);
}

Vec3f Terrain::GeneratePos(int globalX, int globalZ, int seed)
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
        height += amplitude * stb_perlin_noise3_seed((float)globalX * frequency, 0.f, (float)globalZ * frequency, 0, 0, 0, seed + i);
    }

    return Vec3f(
        CellSize * ((float)globalX - 0.5f * (float)(CellsPerTileX * NumTilesX)),
        height,
        CellSize * ((float)globalZ - 0.5f * (float)(CellsPerTileZ * NumTilesZ))
    );
}

}

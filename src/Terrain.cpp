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

static Vec3f TriangleNormal(const Vec3f& p0, const Vec3f& p1, const Vec3f& p2)
{
    return math::normalize(math::cross(p1 - p0, p2 - p0));
}

// TODO: We could just write directly to the intermediate buffer when mapped.
static void CreateVertexBuffer(Renderer& renderer, Terrain::VertexBuffer& vb, int vertexCount, const void* data)
{
    renderer.CreateBuffer(vb.buffer, vb.intermediateBuffer, vertexCount * sizeof(Vertex), data);
    vb.view.BufferLocation = vb.buffer->GetGPUVirtualAddress();
    vb.view.SizeInBytes = vertexCount * sizeof(Vertex);
    vb.view.StrideInBytes = sizeof(Vertex);
}

static void CreateIndexBuffer(Renderer& renderer, Terrain::IndexBuffer& ib, int indexCount, const void* data)
{
    renderer.CreateBuffer(ib.buffer, ib.intermediateBuffer, indexCount * sizeof(uint16_t), data);
    ib.view.BufferLocation = ib.buffer->GetGPUVirtualAddress();
    ib.view.Format = DXGI_FORMAT_R16_UINT;
    ib.view.SizeInBytes = indexCount * sizeof(uint16_t);
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
    ID3D12GraphicsCommandList2& commandList = renderer.GetDirectCommandList();
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

void Terrain::BuildIndexBuffer(Renderer& renderer)
{
    // TODO: Consider triangle strips or other topology?
    auto indexData = std::make_unique<uint16_t[]>(IndicesPerTile);
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

    CreateIndexBuffer(renderer, m_indexBuffer, IndicesPerTile, indexData.get());
}

void Terrain::BuildTile(Renderer& renderer, int tileX, int tileZ, int seed)
{
    assert(0 <= tileX && tileX < NumTilesX);
    assert(0 <= tileZ && tileZ < NumTilesZ);

    auto vertexData = std::make_unique<Vertex[]>(VertsPerTile);

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
            Vec3f left = (x > 0) ? vertexData[VertexIndex(x - 1, z)].position : GeneratePos(globalX - 1, globalZ, seed);
            Vec3f down = (z > 0) ? vertexData[VertexIndex(x, z - 1)].position : GeneratePos(globalX, globalZ - 1, seed);
            Vec3f right = (x < CellsPerTileX) ? vertexData[VertexIndex(x + 1, z)].position : GeneratePos(globalX + 1, globalZ, seed);
            Vec3f up    = (z < CellsPerTileZ) ? vertexData[VertexIndex(x, z + 1)].position : GeneratePos(globalX, globalZ + 1, seed);
            Vec3f downLeft = (x > 0 && z > 0) ? vertexData[VertexIndex(x - 1, z - 1)].position : GeneratePos(globalX - 1, globalZ - 1, seed);
            Vec3f upRight = (x < CellsPerTileX && z < CellsPerTileZ) ? vertexData[VertexIndex(x + 1, z + 1)].position : GeneratePos(globalX + 1, globalZ + 1, seed);

            Vec3f normal = Vec3fZero;
            normal += TriangleNormal(downLeft, left, v.position);
            normal += TriangleNormal(downLeft, v.position, down);
            normal += TriangleNormal(left, up, v.position);
            normal += TriangleNormal(down, v.position, right);
            normal += TriangleNormal(v.position, up, upRight);
            normal += TriangleNormal(v.position, upRight, right);
            v.normal = normal / 6.f;
        }
    }

    int tileIndex = TileIndex(tileX, tileZ);
    CreateVertexBuffer(renderer, m_tileVertexBuffers[tileIndex], VertsPerTile, vertexData.get());
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

    CreateVertexBuffer(renderer, m_waterVertexBuffer, (int)std::size(WaterVerts), WaterVerts);
    CreateIndexBuffer(renderer, m_waterIndexBuffer, (int)std::size(WaterIndices), WaterIndices);
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

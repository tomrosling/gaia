#include "Terrain.hpp"
#include "Renderer.hpp"

namespace gaia
{

const int CellsX = 63;
const int CellsZ = 63;
const int NumVerts = (CellsX + 1) * (CellsZ + 1);
const int NumIndices = 2 * 3 * CellsX * CellsZ;
const float CellSize = 0.25f;
static_assert(NumVerts <= USHRT_MAX, "Index format too small");

static int VertexAddress(int x, int z) 
{
    assert(0 <= x && x <= CellsX);
    assert(0 <= z && z <= CellsZ);
    return (CellsX + 1) * z + x;
}

static Vec3f TriangleNormal(const Vec3f& p0, const Vec3f& p1, const Vec3f& p2)
{
    return math::normalize(math::cross(p1 - p0, p2 - p0));
};

Terrain::Terrain(Renderer& renderer)
{
    auto vertexData = std::make_unique<Vertex[]>(NumVerts);
    auto indexData = std::make_unique<uint16_t[]>(NumIndices);

    // Initialise pos, col / generate heights
    for (int z = 0; z <= CellsZ; ++z)
    {
        for (int x = 0; x <= CellsX; ++x)
        {
            Vertex& v = vertexData[VertexAddress(x, z)];
            float height = 0.2f + cosf(0.2f * (float)x) * sinf(0.3f * (float)z);
            height += 0.2f * (float)rand() / (float)RAND_MAX;
            v.position.x = CellSize * ((float)x - 0.5f * (float)CellsX);
            v.position.y = height;
            v.position.z = CellSize * ((float)z - 0.5f * (float)CellsZ);

            // Leave v.normal uninitialised for now...

            uint8_t brightness = 0x80;
            v.colour[0] = brightness;
            v.colour[1] = brightness;
            v.colour[2] = brightness;
            v.colour[3] = 0xff;
        }
    }

    // Generate normals
    for (int z = 0; z <= CellsZ; ++z)
    {
        for (int x = 0; x <= CellsX; ++x)
        {
            Vertex& v = vertexData[VertexAddress(x, z)];

            Vec3f normal = { 0.f, 0.f, 0.f };
            if (x > 0)
            {
                const Vec3f& left = vertexData[VertexAddress(x - 1, z)].position;
                if (z > 0)
                {
                    // Below left
                    // should this account for both triangles?
                    const Vec3f& down = vertexData[VertexAddress(x, z - 1)].position;
                    normal += TriangleNormal(left, v.position, down);
                }
                if (z < CellsZ)
                {
                    // Above left
                    const Vec3f& up = vertexData[VertexAddress(x, z + 1)].position;
                    normal += TriangleNormal(left, up, v.position);
                }
            }

            if (x < CellsX)
            {
                const Vec3f& right = vertexData[VertexAddress(x + 1, z)].position;
                if (z > 0)
                {
                    // Below right
                    // should this account for both triangles?
                    const Vec3f& down = vertexData[VertexAddress(x, z - 1)].position;
                    normal += TriangleNormal(down, v.position, right);
                }
                if (z < CellsZ)
                {
                    // Above right
                    const Vec3f& up = vertexData[VertexAddress(x, z + 1)].position;
                    normal += TriangleNormal(v.position, up, right);
                }
            }

            v.normal = math::normalize(normal);
        }
    }

    // Init indices
    // TODO: Consider triangle strips or other topology?
    for (int z = 0; z < CellsZ; ++z)
    {
        for (int x = 0; x < CellsX; ++x)
        {
            uint16_t* p = &indexData[2 * 3 * (CellsX * z + x)];
            p[0] = (CellsX + 1) * (z + 0) + (x + 0);
            p[1] = (CellsX + 1) * (z + 1) + (x + 0);
            p[2] = (CellsX + 1) * (z + 1) + (x + 1);
            p[3] = (CellsX + 1) * (z + 0) + (x + 0);
            p[4] = (CellsX + 1) * (z + 1) + (x + 1);
            p[5] = (CellsX + 1) * (z + 0) + (x + 1);
        }
    }

    renderer.BeginUploads();

    // TODO: just keep a vector (or static array) on Renderer of intermediate buffers, cleared on EndUploads
    auto createVertexBuffer = [](Renderer& renderer, VertexBuffer& vb, ComPtr<ID3D12Resource>& intermediateBuffer, int vertexCount, const void* data)
    {
        renderer.CreateBuffer(vb.buffer, intermediateBuffer, vertexCount * sizeof(Vertex), data);
        vb.view.BufferLocation = vb.buffer->GetGPUVirtualAddress();
        vb.view.SizeInBytes = vertexCount * sizeof(Vertex);
        vb.view.StrideInBytes = sizeof(Vertex);
    };

    auto createIndexBuffer = [](Renderer& renderer, IndexBuffer& ib, ComPtr<ID3D12Resource>& intermediateBuffer, int indexCount, const void* data) {
        renderer.CreateBuffer(ib.buffer, intermediateBuffer, indexCount * sizeof(uint16_t), data);
        ib.view.BufferLocation = ib.buffer->GetGPUVirtualAddress();
        ib.view.Format = DXGI_FORMAT_R16_UINT;
        ib.view.SizeInBytes = indexCount * sizeof(uint16_t);
    };

    // Main terrain data
    ComPtr<ID3D12Resource> intermediateVB;
    createVertexBuffer(renderer, m_vertexBuffer, intermediateVB, NumVerts, vertexData.get());
    ComPtr<ID3D12Resource> intermediateIB;
    createIndexBuffer(renderer, m_indexBuffer, intermediateIB, NumIndices, indexData.get());

    const float HalfGridSizeX = 0.5f * CellSize * (float)CellsX;
    const float HalfGridSizeZ = 0.5f * CellSize * (float)CellsZ;
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

    // Water plane
    ComPtr<ID3D12Resource> intermediateWaterVB;
    createVertexBuffer(renderer, m_waterVertexBuffer, intermediateWaterVB, (int)std::size(WaterVerts), WaterVerts);
    ComPtr<ID3D12Resource> intermediateWaterIB;
    createIndexBuffer(renderer, m_waterIndexBuffer, intermediateWaterIB, (int)std::size(WaterIndices), WaterIndices);

    renderer.EndUploads();
}

void Terrain::Render(Renderer& renderer)
{
    renderer.SetModelMatrix(Mat4fIdentity);

    ID3D12GraphicsCommandList2& commandList = renderer.GetDirectCommandList();
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Render the terrain itself.
    commandList.IASetVertexBuffers(0, 1, &m_vertexBuffer.view);
    commandList.IASetIndexBuffer(&m_indexBuffer.view);
    commandList.DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);

    // Render "water".
    commandList.IASetVertexBuffers(0, 1, &m_waterVertexBuffer.view);
    commandList.IASetIndexBuffer(&m_waterIndexBuffer.view);
    commandList.DrawIndexedInstanced(m_waterIndexBuffer.view.SizeInBytes / sizeof(uint16_t), 1, 0, 0, 0);
}

}

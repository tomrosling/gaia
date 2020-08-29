#include "Terrain.hpp"
#include "Renderer.hpp"

namespace gaia
{

using namespace DirectX;

const int CellsX = 63;
const int CellsZ = 63;
const int NumVerts = (CellsX + 1) * (CellsZ + 1);
const int NumIndices = 2 * 3 * CellsX * CellsZ;
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
    : m_vertexData(std::make_unique<Vertex[]>(NumVerts))
    , m_indexData(std::make_unique<uint16_t[]>(NumIndices))
{
    // Initialise pos, col / generate heights
    for (int z = 0; z <= CellsZ; ++z)
    {
        for (int x = 0; x <= CellsX; ++x)
        {
            Vertex& v = m_vertexData[VertexAddress(x, z)];
            float height = cosf(0.2f * (float)x) * sinf(0.3f * (float)z);
            height += 0.2f * (float)rand() / (float)RAND_MAX;
            v.position.x = 0.25f * ((float)x - 0.5f * (float)CellsX);
            v.position.y = height;
            v.position.z = 0.25f * ((float)z - 0.5f * (float)CellsZ);

            // Leave v.normal uninitialised for now...

            uint8_t brightness = 0x80;
            v.colour[0] = brightness;
            v.colour[1] = brightness;
            v.colour[2] = brightness;
        }
    }

    // Generate normals
    for (int z = 0; z <= CellsZ; ++z)
    {
        for (int x = 0; x <= CellsX; ++x)
        {
            Vertex& v = m_vertexData[VertexAddress(x, z)];

            Vec3f normal = { 0.f, 0.f, 0.f };
            if (x > 0)
            {
                const Vec3f& left = m_vertexData[VertexAddress(x - 1, z)].position;
                if (z > 0)
                {
                    // Below left
                    // should this account for both triangles?
                    const Vec3f& down = m_vertexData[VertexAddress(x, z - 1)].position;
                    normal += TriangleNormal(left, v.position, down);
                }
                if (z < CellsZ)
                {
                    // Above left
                    const Vec3f& up = m_vertexData[VertexAddress(x, z + 1)].position;
                    normal += TriangleNormal(left, up, v.position);
                }
            }

            if (x < CellsX)
            {
                const Vec3f& right = m_vertexData[VertexAddress(x + 1, z)].position;
                if (z > 0)
                {
                    // Below right
                    // should this account for both triangles?
                    const Vec3f& down = m_vertexData[VertexAddress(x, z - 1)].position;
                    normal += TriangleNormal(down, v.position, right);
                }
                if (z < CellsZ)
                {
                    // Above right
                    const Vec3f& up = m_vertexData[VertexAddress(x, z + 1)].position;
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
            uint16_t* p = &m_indexData[2 * 3 * (CellsX * z + x)];
            p[0] = (CellsX + 1) * (z + 0) + (x + 0);
            p[1] = (CellsX + 1) * (z + 1) + (x + 0);
            p[2] = (CellsX + 1) * (z + 1) + (x + 1);
            p[3] = (CellsX + 1) * (z + 0) + (x + 0);
            p[4] = (CellsX + 1) * (z + 1) + (x + 1);
            p[5] = (CellsX + 1) * (z + 0) + (x + 1);
        }
    }

    renderer.BeginUploads();

    // Upload vertex buffer (must be done via an intermediate resource)
    ComPtr<ID3D12Resource> intermediateVB;
    renderer.CreateBuffer(m_vertexBuffer, intermediateVB, NumVerts * sizeof(Vertex), m_vertexData.get());

    // Create vertex buffer view
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = NumVerts * sizeof(Vertex);
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);

    // Upload index buffer
    ComPtr<ID3D12Resource> intermediateIB;
    renderer.CreateBuffer(m_indexBuffer, intermediateIB, NumIndices * sizeof(uint16_t), m_indexData.get());

    // Create index buffer view
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
    m_indexBufferView.SizeInBytes = NumIndices * sizeof(uint16_t);

    renderer.EndUploads();
}

void Terrain::Render(Renderer& renderer)
{
    renderer.SetModelMatrix(math::identity<Mat4f>());
    
    ID3D12GraphicsCommandList2& commandList = renderer.GetDirectCommandList();
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList.IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList.IASetIndexBuffer(&m_indexBufferView);
    commandList.DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
}

}

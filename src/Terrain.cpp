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

// TODO: Maths lib!
static XMFLOAT3 XMFloat3Add(const XMFLOAT3& u, const XMFLOAT3& v)
{
    return { u.x + v.x, u.y + v.y, u.z + v.z };
}

static XMFLOAT3 XMFloat3Sub(const XMFLOAT3& u, const XMFLOAT3& v)
{
    return { u.x - v.x, u.y - v.y, u.z - v.z };
}

static XMFLOAT3 XMFloat3Normalise(const XMFLOAT3& v)
{
    float lengthSq = v.x * v.x + v.y * v.y + v.z * v.z;
    assert(lengthSq > 0.f);
    float length = sqrtf(lengthSq);
    return { v.x / length, v.y / length, v.z / length };
}

static XMFLOAT3 XMFloat3Cross(const XMFLOAT3& u, const XMFLOAT3& v)
{
    return {
        u.y * v.z - u.z * v.y,
        u.z * v.x - u.x * v.z,
        u.x * v.y - u.y * v.x
    };
}

static XMFLOAT3 TriangleNormal(const XMFLOAT3& p0, const XMFLOAT3& p1, const XMFLOAT3& p2)
{
    XMFLOAT3 d1 = XMFloat3Sub(p1, p0);
    XMFLOAT3 d2 = XMFloat3Sub(p2, p0);
    return XMFloat3Normalise(XMFloat3Cross(d1, d2));
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

            XMFLOAT3 normal = { 0.f, 0.f, 0.f };
            if (x > 0)
            {
                const XMFLOAT3& left = m_vertexData[VertexAddress(x - 1, z)].position;
                if (z > 0)
                {
                    // Below left
                    // should this account for both triangles?
                    const XMFLOAT3& down = m_vertexData[VertexAddress(x, z - 1)].position;
                    XMFLOAT3 triNrm = TriangleNormal(left, v.position, down);
                    normal = XMFloat3Add(normal, triNrm);
                }
                if (z < CellsZ)
                {
                    // Above left
                    const DirectX::XMFLOAT3& up = m_vertexData[VertexAddress(x, z + 1)].position;
                    XMFLOAT3 triNrm = TriangleNormal(left, up, v.position);
                    normal = XMFloat3Add(normal, triNrm);
                }
            }

            if (x < CellsX)
            {
                const DirectX::XMFLOAT3& right = m_vertexData[VertexAddress(x + 1, z)].position;
                if (z > 0)
                {
                    // Below right
                    // should this account for both triangles?
                    const XMFLOAT3& down = m_vertexData[VertexAddress(x, z - 1)].position;
                    XMFLOAT3 triNrm = TriangleNormal(down, v.position, right);
                    normal = XMFloat3Add(normal, triNrm);
                }
                if (z < CellsZ)
                {
                    // Above right
                    const XMFLOAT3& up = m_vertexData[VertexAddress(x, z + 1)].position;
                    XMFLOAT3 triNrm = TriangleNormal(v.position, up, right);
                    normal = XMFloat3Add(normal, triNrm);
                }
            }

            v.normal = XMFloat3Normalise(normal);
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
    renderer.SetModelMatrix(DirectX::XMMatrixIdentity());
    
    ID3D12GraphicsCommandList2& commandList = renderer.GetDirectCommandList();
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList.IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList.IASetIndexBuffer(&m_indexBufferView);
    commandList.DrawIndexedInstanced(NumIndices, 1, 0, 0, 0);
}

}

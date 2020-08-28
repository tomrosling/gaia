#include "Terrain.hpp"
#include "Renderer.hpp"

namespace gaia
{

const int CellsX = 31;
const int CellsZ = 31;
const int NumVerts = (CellsX + 1) * (CellsZ + 1);
const int NumIndices = 2 * 3 * CellsX * CellsZ;
static_assert(NumVerts <= USHRT_MAX, "Index format too small");

Terrain::Terrain(Renderer& renderer)
    : m_vertexData(std::make_unique<Vertex[]>(NumVerts))
    , m_indexData(std::make_unique<uint16_t[]>(NumIndices))
{
    // Init vertices
    for (int z = 0; z <= CellsZ; ++z)
    {
        for (int x = 0; x <= CellsX; ++x)
        {
            Vertex& v = m_vertexData[(CellsX + 1) * z + x];
            float height = (float)rand() / (float)RAND_MAX;
            v.position.x = (float)x - 0.5f * (float)CellsX;
            v.position.y = height;
            v.position.z = (float)z - 0.5f * (float)CellsZ;

            uint8_t brightness = (uint8_t)(height * 200.f + 40.f);
            v.colour[0] = brightness;
            v.colour[1] = brightness;
            v.colour[2] = brightness;
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

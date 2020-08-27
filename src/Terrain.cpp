#include "Terrain.hpp"
#include "Renderer.hpp"

namespace gaia
{

static const Renderer::Vertex VertexData[] = {
    { { -10.f, 0.f, -10.f }, { 0x80, 0x80, 0x80 } },
    { { -10.f, 0.f,  10.f }, { 0x80, 0x80, 0x80 } },
    { {  10.f, 0.f,  10.f }, { 0x80, 0x80, 0x80 } },
    { {  10.f, 0.f, -10.f }, { 0x80, 0x80, 0x80 } }
};

static const uint16_t IndexData[] = {
    0, 1, 2,
    2, 3, 0
};

Terrain::Terrain(Renderer& renderer)
{
    renderer.BeginUploads();

    // Upload vertex buffer (must be done via an intermediate resource)
    ComPtr<ID3D12Resource> intermediateVB;
    renderer.CreateBuffer(m_vertexBuffer, intermediateVB, sizeof(VertexData), VertexData);

    // Create vertex buffer view
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = sizeof(VertexData);
    m_vertexBufferView.StrideInBytes = sizeof(Renderer::Vertex);

    // Upload index buffer
    ComPtr<ID3D12Resource> intermediateIB;
    renderer.CreateBuffer(m_indexBuffer, intermediateIB, sizeof(IndexData), IndexData);

    // Create index buffer view
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
    m_indexBufferView.SizeInBytes = sizeof(IndexData);

    renderer.EndUploads();
}

void Terrain::Render(Renderer& renderer)
{
    renderer.SetModelMatrix(DirectX::XMMatrixIdentity());
    
    ID3D12GraphicsCommandList2& commandList = renderer.GetDirectCommandList();
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList.IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList.IASetIndexBuffer(&m_indexBufferView);
    commandList.DrawIndexedInstanced((UINT)std::size(IndexData), 1, 0, 0, 0);
}

}

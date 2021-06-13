#include "StaticMesh.hpp"
#include "Renderer.hpp"

namespace gaia
{

void StaticMesh::Init(Renderer& renderer, const Span<const uchar>& vertexData, int vertexStride, const Span<const uint16>& indexData)
{
    renderer.CreateBuffer(m_vb.buffer, m_vb.intermediateBuffer, vertexData.Size(), vertexData.Data());
    m_vb.view.BufferLocation = m_vb.buffer->GetGPUVirtualAddress();
    m_vb.view.SizeInBytes = vertexData.Size();
    m_vb.view.StrideInBytes = vertexStride;

    renderer.CreateBuffer(m_ib.buffer, m_ib.intermediateBuffer, indexData.Size() * sizeof(uint16), indexData.Data());
    m_ib.view.BufferLocation = m_ib.buffer->GetGPUVirtualAddress();
    m_ib.view.Format = DXGI_FORMAT_R16_UINT;
    m_ib.view.SizeInBytes = indexData.Size() * sizeof(uint16);
}

void StaticMesh::Render(Renderer& renderer)
{
    ID3D12GraphicsCommandList& commandList = renderer.GetDirectCommandList();
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList.IASetVertexBuffers(0, 1, &m_vb.view);
    commandList.IASetIndexBuffer(&m_ib.view);
    commandList.DrawIndexedInstanced(m_ib.view.SizeInBytes / sizeof(uint16), 1, 0, 0, 0);
}

}

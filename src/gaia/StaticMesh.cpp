#include "StaticMesh.hpp"
#include "Renderer.hpp"

namespace gaia
{

void StaticMesh::Init(Renderer& renderer, const Span<const uchar>& vertexData, int vertexStride, const Span<const uint16>& indexData)
{
    m_vb = renderer.CreateVertexBuffer(vertexData, vertexStride);
    m_ib = renderer.CreateIndexBuffer(Span<const uchar>((const uchar*)indexData.Data(), indexData.Size() * sizeof(uint16)), DXGI_FORMAT_R16_UINT);
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

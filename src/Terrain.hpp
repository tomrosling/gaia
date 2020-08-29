#pragma once

namespace gaia
{

class Renderer;
struct Vertex;

class Terrain
{
public:
    Terrain(Renderer& renderer);
    void Render(Renderer& renderer);

private:
    struct VertexBuffer
    {
        ComPtr<ID3D12Resource> buffer;
        D3D12_VERTEX_BUFFER_VIEW view = {};        
    };

    struct IndexBuffer
    {
        ComPtr<ID3D12Resource> buffer;
        D3D12_INDEX_BUFFER_VIEW view = {};
    };

    VertexBuffer m_vertexBuffer;
    IndexBuffer m_indexBuffer;
    VertexBuffer m_waterVertexBuffer;
    IndexBuffer m_waterIndexBuffer;
};

}

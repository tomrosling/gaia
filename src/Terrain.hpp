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
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};

    std::unique_ptr<Vertex[]> m_vertexData;
    std::unique_ptr<uint16_t[]> m_indexData;
};

}

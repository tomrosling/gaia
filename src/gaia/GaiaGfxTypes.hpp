#pragma once

namespace gaia
{

enum class ShaderStage
{
    Vertex,
    Hull,
    Domain,
    Pixel,
    Count
};

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

}

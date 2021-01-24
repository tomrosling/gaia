#pragma once

namespace gaia
{

class Renderer;

class Skybox
{
public:
    bool Init(Renderer& renderer);
    void Render(Renderer& renderer);

private:
    bool CreatePipelineState(Renderer& renderer, ID3DBlob* vertexShader, ID3DBlob* pixelShader);

    ComPtr<ID3D12PipelineState> m_pipelineState;

    ComPtr<ID3D12Resource> m_intermediateVertexBuffer;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    ComPtr<ID3D12Resource> m_intermediateIndexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;

    ComPtr<ID3D12Resource> m_cubemapTexResource;
    ComPtr<ID3D12Resource> m_intermediateCubemapTexResource;
    int m_cubemapSrvIndex = -1;

    uint64 m_uploadFenceVal = 0;
};

}

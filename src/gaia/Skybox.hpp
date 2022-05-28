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

    VertexBuffer m_vertexBuffer;
    IndexBuffer m_indexBuffer;

    ComPtr<ID3D12Resource> m_cubemapTexResource;
    int m_cubemapSrvIndex = -1;
};

}

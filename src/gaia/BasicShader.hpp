#pragma once

namespace gaia
{

class Renderer;

struct BasicVertex
{
    Vec3f pos;
    Vec3f nrm;
    Vec4u8 col;
};

class BasicShader
{
public:
    bool Init(Renderer& renderer);
    void Bind(Renderer& renderer);

private:
    bool CreatePipelineState(Renderer& renderer, ID3DBlob* vertexShader, ID3DBlob* pixelShader);

    ComPtr<ID3D12PipelineState> m_pipelineState;
};

}

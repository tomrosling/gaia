#pragma once

namespace gaia {

class Renderer;

class GenerateMips
{
public:
    bool Init(Renderer& renderer);
    void Compute(Renderer& renderer, ID3D12Resource* texture);

private:
    bool CreateRootSignature(Renderer& renderer);
    bool CreatePipelineState(Renderer& renderer);

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
};

}

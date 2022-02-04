#pragma once

namespace gaia
{

class Renderer;

class TerrainComputeNormals
{
public:
    bool Init(Renderer& renderer);
    void Compute(Renderer& renderer, ID3D12Resource* srcHeightMap, ID3D12Resource* dstNormalMap, Vec2i uvMin, Vec2i uvMax, int level);

private:
    bool CreateRootSignature(Renderer& renderer);

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
};

} // namespace gaia

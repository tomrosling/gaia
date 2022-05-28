#pragma once

interface ID3D12Resource;

namespace gaia
{

class CommandQueue;

class UploadManager
{
public:
    void AddIntermediateResource(ID3D12Resource* resource);
    void BeginFrame(CommandQueue& commandQueue);
    void SetFenceValue(UINT64 fenceValue) { m_fenceValue = fenceValue; }

private:
    std::vector<ComPtr<ID3D12Resource>> m_intermediateResources;
    UINT64 m_fenceValue;
};

}

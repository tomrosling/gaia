#pragma once

namespace gaia
{

class CommandQueue
{
public:
    CommandQueue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type);

    ID3D12CommandQueue* GetCommandQueue() { return m_commandQueue.Get(); }
    [[nodiscard]] UINT64 Execute(ID3D12GraphicsCommandList2* commandList);
    [[nodiscard]] UINT64 SignalFence();
    void WaitFence(UINT64 value);
    void Flush();

private:
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_fenceValue = 0;
    D3D12_COMMAND_LIST_TYPE m_type;
};

}

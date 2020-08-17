#pragma once
#include <d3d12.h>
#include <wrl/client.h>

namespace gaia
{

class CommandQueue
{
public:
    CommandQueue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type);

    ID3D12CommandQueue* GetCommandQueue() { return m_commandQueue.Get(); }
    UINT64 Execute(ID3D12GraphicsCommandList2* commandList);
    UINT64 SignalFence();
    void WaitFence(UINT64 value);
    void Flush();

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_fenceValue = 0;
    D3D12_COMMAND_LIST_TYPE m_type;
};

}

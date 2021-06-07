#include "CommandQueue.hpp"

namespace gaia
{

CommandQueue::CommandQueue(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
    : m_type(type)
{
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.Type = type;
    device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_commandQueue));
    Assert(m_commandQueue);

    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    Assert(m_fence);

    m_fenceEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
    Assert(m_fenceEvent);
}

UINT64 CommandQueue::Execute(ID3D12GraphicsCommandList2* commandList)
{
    commandList->Close();
    ID3D12CommandList* lists[] = { commandList };
    m_commandQueue->ExecuteCommandLists(1, lists);
    return SignalFence();
}

UINT64 CommandQueue::SignalFence()
{
    m_commandQueue->Signal(m_fence.Get(), ++m_fenceValue);
    return m_fenceValue;
}

void CommandQueue::WaitFence(UINT64 value)
{
    m_fence->SetEventOnCompletion(value, m_fenceEvent);
    DWORD ret = ::WaitForSingleObject(m_fenceEvent, 1000);
    Assert(ret == WAIT_OBJECT_0);
}

void CommandQueue::Flush()
{
    UINT64 value = SignalFence();
    WaitFence(value);
}

}

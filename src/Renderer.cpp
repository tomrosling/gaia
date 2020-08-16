#include "renderer.hpp"
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <cassert>

namespace gaia
{

Renderer::Renderer() = default;

Renderer::~Renderer()
{
    if (m_created)
    {
        // Ensure all commands are flushed before shutting down.
        m_commandQueue->Signal(m_fence.Get(), ++m_fenceValue);
        m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        DWORD ret = ::WaitForSingleObject(m_fenceEvent, 1000);
        assert(ret == WAIT_OBJECT_0);
    }
}

int Renderer::Create(HWND hwnd)
{
#ifdef _DEBUG
    // Enable debug layer
    ComPtr<ID3D12Debug> debugInterface;
    if (!SUCCEEDED(::D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface))))
        return 1;

    debugInterface->EnableDebugLayer();
#endif

    // Create factory
    UINT dxgiFactoryFlags = 0;
#ifdef _DEBUG
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    HRESULT result = ::CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory));

    // Find a hardware adapter that supports DX12
    for (UINT i = 0; m_factory->EnumAdapters1(i, &m_adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        m_adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;

        if (SUCCEEDED(::D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
            break;
    }

    // Create device
    if (!SUCCEEDED(::D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device))))
        return 1;

    // Create command queue
    D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
    commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!SUCCEEDED(m_device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&m_commandQueue))))
        return 1;

    // Create a fence
    if (!SUCCEEDED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
        return 1;

    m_fenceEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_fenceEvent);
    
    // Create swapchain
    ComPtr<IDXGISwapChain1> swapChain1;
    DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
    swapchainDesc.BufferCount = BackbufferCount;
    swapchainDesc.Width = 800;
    swapchainDesc.Height = 600;
    swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchainDesc.SampleDesc.Count = 1;
    if (!SUCCEEDED(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &swapchainDesc, nullptr, nullptr, &swapChain1)))
        return 1;

    // Cast to IDXGISwapChain3
    if (!SUCCEEDED(swapChain1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_swapChain)))
        return 1;

    m_currentBuffer = m_swapChain->GetCurrentBackBufferIndex();

    // Create RTV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = BackbufferCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!SUCCEEDED(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvDescHeap))))
        return 1;

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create an RTV for each frame
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < BackbufferCount; ++i)
    {
        if (!SUCCEEDED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))))
            return 1;

        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(m_rtvDescriptorSize);
    }

    // Create command allocators
    for (UINT i = 0; i < BackbufferCount; ++i)
    {
        if (!SUCCEEDED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]))))
            return 1;
    }

    // Create command list (we only need one)
    if (!SUCCEEDED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_currentBuffer].Get(), nullptr, IID_PPV_ARGS(&m_commandList))))
        return 1;

    m_commandList->Close();

    m_created = true;
    return 0;
}

void Renderer::Render()
{
    // Prepare for next frame
    ID3D12CommandAllocator* commandAllocator = m_commandAllocators[m_currentBuffer].Get();
    ID3D12Resource* backBuffer = m_renderTargets[m_currentBuffer].Get();

    commandAllocator->Reset();
    m_commandList->Reset(commandAllocator, nullptr);

    CD3DX12_RESOURCE_BARRIER barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    m_commandList->ResourceBarrier(1, &barrier1);

    // Clear backbuffer
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart(), m_currentBuffer, m_rtvDescriptorSize);

    FLOAT clearColor[] = { 0.8f, 0.5f, 0.8f, 1.0f };
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    CD3DX12_RESOURCE_BARRIER barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &barrier2);

    // Submit draw (etc) commands
    m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    // Present buffer
    m_frameFenceValues[m_currentBuffer] = ++m_fenceValue;
    m_commandQueue->Signal(m_fence.Get(), m_frameFenceValues[m_currentBuffer]);
    m_swapChain->Present(1, 0);
    m_currentBuffer = m_swapChain->GetCurrentBackBufferIndex();

    // Wait for previous frame's fence
    m_fence->SetEventOnCompletion(m_frameFenceValues[m_currentBuffer], m_fenceEvent);
    ::WaitForSingleObject(m_fenceEvent, 1000);
}

}

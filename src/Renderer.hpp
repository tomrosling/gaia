#pragma once
#include <windows.h>
#include <wrl/client.h>

interface IDXGIFactory4;
interface IDXGIAdapter1;
interface ID3D12Device;
interface ID3D12CommandQueue;
interface ID3D12GraphicsCommandList;
interface IDXGISwapChain3;
interface ID3D12DescriptorHeap;
interface ID3D12Resource;
interface ID3D12CommandAllocator;
interface ID3D12Fence;

namespace gaia
{

class Renderer
{
public:
    Renderer();
    ~Renderer();

    int Create(HWND hwnd);
    void Render();

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    static const UINT BackbufferCount = 2;

    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<IDXGIAdapter1> m_adapter;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvDescHeap;
    ComPtr<ID3D12Resource> m_renderTargets[BackbufferCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[BackbufferCount];
    ComPtr<ID3D12Fence> m_fence;

    HANDLE m_fenceEvent = nullptr;
    UINT64 m_fenceValue = 0;
    UINT64 m_frameFenceValues[BackbufferCount] = {};

    UINT m_currentBuffer = 0;
    UINT m_rtvDescriptorSize = 0;
    bool m_created = false;
};

}

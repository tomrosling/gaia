#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl/client.h>
#include <memory>

#include <d3d12.h>
#include <DirectXMath.h>

interface IDXGIFactory4;
interface IDXGIAdapter1;
interface ID3D12Device;
interface ID3D12CommandQueue;
interface ID3D12GraphicsCommandList2;
interface IDXGISwapChain3;
interface ID3D12DescriptorHeap;
interface ID3D12Resource;
interface ID3D12CommandAllocator;
interface ID3D12Fence;

namespace gaia
{

class CommandQueue;

class Renderer
{
public:
    Renderer();
    ~Renderer();

    int Create(HWND hwnd);
    void Render();

private:
    void BeginFrame(ID3D12CommandAllocator* commandAllocator, ID3D12Resource* backBuffer);
    void EndFrame(ID3D12Resource* backBuffer);

    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    static const UINT BackbufferCount = 2;

    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<IDXGIAdapter1> m_adapter;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12GraphicsCommandList2> m_commandList;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvDescHeap;
    ComPtr<ID3D12Resource> m_renderTargets[BackbufferCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[BackbufferCount];

    std::unique_ptr<CommandQueue> m_directCommandQueue;

    UINT64 m_frameFenceValues[BackbufferCount] = {};
    UINT m_currentBuffer = 0;
    UINT m_rtvDescriptorSize = 0;
    bool m_created = false;
};

}

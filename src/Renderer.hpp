#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl/client.h>
#include <memory>

#include <d3d12.h>
#include <DirectXMath.h>

interface IDXGIFactory4;
interface IDXGIAdapter1;
interface ID3D12Device2;
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

    bool Create(HWND hwnd);
    bool CreateHelloTriangle();
    void Render();

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    void BeginFrame(ID3D12CommandAllocator* commandAllocator, ID3D12Resource* backBuffer);
    void EndFrame(ID3D12Resource* backBuffer);
    void CreateBuffer(ComPtr<ID3D12Resource>& bufferOut, ComPtr<ID3D12Resource>& intermediateBuffer, size_t size, const void* data);

    static const UINT BackbufferCount = 2;

    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<IDXGIAdapter1> m_adapter;
    ComPtr<ID3D12Device2> m_device;
    ComPtr<ID3D12GraphicsCommandList2> m_directCommandList;
    ComPtr<ID3D12GraphicsCommandList2> m_copyCommandList;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvDescHeap;
    ComPtr<ID3D12Resource> m_renderTargets[BackbufferCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[BackbufferCount];
    ComPtr<ID3D12CommandAllocator> m_copyCommandAllocator;

    std::unique_ptr<CommandQueue> m_directCommandQueue;
    std::unique_ptr<CommandQueue> m_copyCommandQueue;

    D3D12_VIEWPORT m_viewport = {};
    D3D12_RECT m_scissorRect = { 0, 0, LONG_MAX, LONG_MAX };

    UINT64 m_frameFenceValues[BackbufferCount] = {};
    UINT m_currentBuffer = 0;
    UINT m_rtvDescriptorSize = 0;
    bool m_created = false;

    // Hello Triangle resources:
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};
};

}

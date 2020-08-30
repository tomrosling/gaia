#pragma once

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

struct Vertex
{
    Vec3f position;
    Vec3f normal;
    uint8_t colour[4];
};

class Renderer
{
public:
    Renderer();
    ~Renderer();

    bool Create(HWND hwnd);
    bool LoadCompiledShaders();
    bool HotloadShaders();
    void CreateHelloTriangle();
    void RenderHelloTriangle();
    void BeginFrame();
    void EndFrame();

    void SetViewMatrix(const Mat4f& viewMat) { m_viewMat = viewMat; }

    // TODO: Something better than this.
    void SetModelMatrix(const Mat4f& modelMat);

    ID3D12GraphicsCommandList2& GetDirectCommandList() { assert(m_directCommandList); return *m_directCommandList.Get(); }
    void BeginUploads();
    void CreateBuffer(ComPtr<ID3D12Resource>& bufferOut, ComPtr<ID3D12Resource>& intermediateBuffer, size_t size, const void* data);
    void EndUploads();

private:
    bool CreateDefaultPipelineState(ID3DBlob* vertexShader, ID3DBlob* pixelShader);

    static const UINT BackbufferCount = 2;

    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<IDXGIAdapter1> m_adapter;
    ComPtr<ID3D12Device2> m_device;
    ComPtr<ID3D12GraphicsCommandList2> m_directCommandList;
    ComPtr<ID3D12GraphicsCommandList2> m_copyCommandList;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvDescHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvDescHeap;
    ComPtr<ID3D12Resource> m_renderTargets[BackbufferCount];
    ComPtr<ID3D12Resource> m_depthBuffer;
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

    Mat4f m_viewMat = math::identity<Mat4f>();
    Mat4f m_projMat = math::identity<Mat4f>();

    // Hello Triangle resources:
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
    ComPtr<ID3D12Resource> m_indexBuffer;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};
};

}

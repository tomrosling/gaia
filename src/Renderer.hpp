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
class GenerateMips;

namespace RootParam
{
enum E
{
    VSSharedConstants,
    PSSharedConstants,
    PSConstantBuffer,
    StaticSamplerState,
    Texture0, // TODO: Could combine these into a single descriptor table, if we
    Texture1, //       can ensure that the descriptors used will be contiguous.
    Count
};
}

class Renderer
{
public:
    Renderer();
    ~Renderer();

    bool Create(HWND hwnd);
    bool ResizeViewport(int width, int height);
    void BeginFrame();
    void EndFrame();
    void WaitCurrentFrame();
    int GetCurrentBuffer() const { return m_currentBuffer; }

    void SetViewMatrix(const Mat4f& viewMat) { m_viewMat = viewMat; }

    ID3D12Device2& GetDevice() { Assert(m_device); return *m_device.Get(); }
    ID3D12RootSignature& GetRootSignature() { Assert(m_rootSignature); return *m_rootSignature.Get(); }
    ID3D12GraphicsCommandList2& GetDirectCommandList() { Assert(m_directCommandList); return *m_directCommandList.Get(); }
    ID3D12GraphicsCommandList2& GetCopyCommandList() { Assert(m_copyCommandList); return *m_copyCommandList.Get(); }
    ID3D12GraphicsCommandList2& GetComputeCommandList() { Assert(m_computeCommandList); return *m_computeCommandList.Get(); }

    void BeginUploads();
    ComPtr<ID3D12Resource> CreateResidentBuffer(size_t size);
    ComPtr<ID3D12Resource> CreateUploadBuffer(size_t size);
    ComPtr<ID3D12Resource> CreateReadbackBuffer(size_t size);
    ComPtr<ID3D12Resource> CreateConstantBuffer(size_t size);
    void CreateBuffer(ComPtr<ID3D12Resource>& bufferOut, ComPtr<ID3D12Resource>& intermediateBuffer, size_t size, const void* data);
    [[nodiscard]] int LoadTexture(ComPtr<ID3D12Resource>& textureOut, ComPtr<ID3D12Resource>& intermediateBuffer, const wchar_t* filepath);
    [[nodiscard]] UINT64 EndUploads();
    void WaitUploads(UINT64 fenceVal);
    void GenerateMips(ID3D12Resource* texture);
    void BindDescriptor(int descIndex, RootParam::E slot);
    
    // TODO: This is a bit janky; the root signature is defined externally but the Renderer
    // is still managing the command queue/command list for compute.
    void BindComputeDescriptor(int descIndex, int slot);

    void BeginCompute();
    [[nodiscard]] UINT64 EndCompute();
    void WaitCompute(UINT64 fenceVal);

    // Descriptors currently use a simple stack allocation scheme,
    // so Frees must be reverse ordered to the Allocates.
    [[nodiscard]] int AllocateConstantBufferViews(ID3D12Resource* (&buffers)[BackbufferCount], UINT size);
    void FreeConstantBufferView(int index);

    // Compute descriptors are kept until the compute operation completes, then discarded.
    // TODO: Tidy the whole descriptor heap stuff up.
    [[nodiscard]] int AllocateComputeUAV(ID3D12Resource* targetResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc);
    [[nodiscard]] int AllocateComputeSRV(ID3D12Resource* targetResource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc);

    D3D12_FEATURE_DATA_ROOT_SIGNATURE GetRootSignatureFeaturedData() const;

    float ReadDepth(int x, int y);
    Vec3f Unproject(Vec3f screenCoords) const;

private:
    bool CreateRootSignature();

    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<IDXGIAdapter1> m_adapter;
    ComPtr<ID3D12Device2> m_device;
    ComPtr<ID3D12GraphicsCommandList2> m_directCommandList;
    ComPtr<ID3D12GraphicsCommandList2> m_copyCommandList;
    ComPtr<ID3D12GraphicsCommandList2> m_computeCommandList;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvDescHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvDescHeap;
    ComPtr<ID3D12DescriptorHeap> m_cbvDescHeaps[BackbufferCount]; // CRB/SRV/UAV descriptors.
    ComPtr<ID3D12DescriptorHeap> m_computeDescHeap;
    ComPtr<ID3D12Resource> m_renderTargets[BackbufferCount];
    ComPtr<ID3D12Resource> m_depthBuffer;
    ComPtr<ID3D12Resource> m_depthReadbackBuffer;
    ComPtr<ID3D12CommandAllocator> m_commandAllocators[BackbufferCount];
    ComPtr<ID3D12CommandAllocator> m_copyCommandAllocator;
    ComPtr<ID3D12CommandAllocator> m_computeCommandAllocator;
    ComPtr<ID3D12RootSignature> m_rootSignature;

    std::unique_ptr<CommandQueue> m_directCommandQueue;
    std::unique_ptr<CommandQueue> m_copyCommandQueue;
    std::unique_ptr<CommandQueue> m_computeCommandQueue;
    std::unique_ptr<gaia::GenerateMips> m_genMips;

    D3D12_VIEWPORT m_viewport = {};
    D3D12_RECT m_scissorRect = { 0, 0, LONG_MAX, LONG_MAX };

    UINT64 m_frameFenceValues[BackbufferCount] = {};
    UINT64 m_depthReadbackFenceValue = 0;
    int m_nextCBVDescIndex = 0;
    int m_nextComputeDescIndex = 0;
    int m_currentBuffer = 0;
    UINT m_rtvDescriptorSize = 0;
    UINT m_cbvDescriptorSize = 0;
    bool m_created = false;

    Mat4f m_viewMat = Mat4fIdentity;
    Mat4f m_projMat = Mat4fIdentity;
};

}

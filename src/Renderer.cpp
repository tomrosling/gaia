#include "renderer.hpp"
#include <DirectXTex/DirectXTex.h>
#include <DDSTextureLoader/DDSTextureLoader12.h>
#include <WICTextureLoader/WICTextureLoader12.h>
#include <examples/imgui_impl_win32.h>
#include <examples/imgui_impl_dx12.h>
#include "CommandQueue.hpp"
#include "GenerateMips.hpp"

namespace gaia
{

static constexpr int TextureAlignment = 256;
static constexpr int CBufferAlignment = 256;
static constexpr int NumCBVDescriptors = 8;
static constexpr int NumComputeDescriptors = 32;

static int GetTexturePitchBytes(int width, int bytesPerTexel)
{
    return math::AlignPow2(width * bytesPerTexel, TextureAlignment);
}

static int CountMips(int width, int height)
{
    if (width == 0 || height == 0)
        return 0;

    return 1u + std::max(math::ILog2(width), math::ILog2(height));
}

struct VSSharedConstants
{
    Mat4f viewMat;
    Mat4f projMat;
};

struct PSSharedConstants
{
    Vec3f camPos;
};

Renderer::Renderer()
{
}

Renderer::~Renderer()
{
    if (m_created)
    {
        // Ensure all commands are flushed before shutting down.
        m_directCommandQueue->Flush();
        m_copyCommandQueue->Flush();

        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
}

bool Renderer::Create(HWND hwnd)
{
#ifdef _DEBUG
    // Enable debug layer
    ComPtr<ID3D12Debug> debugInterface;
    if (FAILED(::D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface))))
        return false;

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
    if (FAILED(::D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device))))
        return false;

    // Create command queues
    m_directCommandQueue = std::make_unique<CommandQueue>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
    m_copyCommandQueue = std::make_unique<CommandQueue>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COPY);
    m_computeCommandQueue = std::make_unique<CommandQueue>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE);

    // Create swapchain
    ComPtr<IDXGISwapChain1> swapChain1;
    DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
    swapchainDesc.BufferCount = BackbufferCount;
    swapchainDesc.Width = 0;  // Leave these at zero for now; ResizeViewport() should be called before rendering.
    swapchainDesc.Height = 0; //
    swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchainDesc.SampleDesc.Count = 1;
    if (!SUCCEEDED(m_factory->CreateSwapChainForHwnd(m_directCommandQueue->GetCommandQueue(), hwnd, &swapchainDesc, nullptr, nullptr, &swapChain1)))
        return false;

    // Cast to IDXGISwapChain3
    if (!SUCCEEDED(swapChain1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_swapChain)))
        return false;

    // Create RTV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = BackbufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvDescHeap))))
        return false;

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create DSV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvDescHeap))))
        return false;

    // Create constant buffer/shader resource view/unordered access view descriptor heap.
    for (auto& cbvHeap : m_cbvDescHeaps)
    {
        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
        cbvHeapDesc.NumDescriptors = NumCBVDescriptors;
        cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(m_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&cbvHeap))))
            return false;
    }

    // Create descriptor heap for compute shaders.
    D3D12_DESCRIPTOR_HEAP_DESC computeHeapDesc = {};
    computeHeapDesc.NumDescriptors = NumComputeDescriptors;
    computeHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    computeHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_device->CreateDescriptorHeap(&computeHeapDesc, IID_PPV_ARGS(&m_computeDescHeap))))
        return false;

    m_cbvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Create command allocators
    for (auto& allocator : m_commandAllocators)
    {
        if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
            return false;
    }

    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_copyCommandAllocator))))
        return false;

    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_computeCommandAllocator))))
        return false;

    // Create command lists
    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_currentBuffer].Get(), nullptr, IID_PPV_ARGS(&m_directCommandList))))
        return false;
    m_directCommandList->Close();

    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, m_copyCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_copyCommandList))))
        return false;
    m_copyCommandList->Close();

    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_computeCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_computeCommandList))))
        return false;
    m_computeCommandList->Close();

    // NOTE: We don't actually create the render and depth targets here, assuming ResizeViewport will be called with an appropriate size.

    if (!CreateRootSignature())
        return false;

    m_genMips = std::make_unique<gaia::GenerateMips>();
    if (!m_genMips->Init(*this))
        return false;
    
    if (!CreateImgui(hwnd))
        return false;

    m_created = true;
    return true;
}

bool Renderer::ResizeViewport(int width, int height)
{
    Assert(width > 0 && height > 0);

    // Tear down existing render targets.
    m_directCommandQueue->Flush();
    m_copyCommandQueue->Flush();
    m_depthBuffer = nullptr;
    for (auto& rt : m_renderTargets)
    {
        rt = nullptr;
    }

    ImGui_ImplDX12_InvalidateDeviceObjects();

    // Recreate the swap chain render targets.
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    m_swapChain->GetDesc(&swapChainDesc);
    if (FAILED(m_swapChain->ResizeBuffers(BackbufferCount, width, height, swapChainDesc.BufferDesc.Format, swapChainDesc.Flags)))
        return false;

    m_currentBuffer = m_swapChain->GetCurrentBackBufferIndex();

    // Create an RTV for each frame.
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < BackbufferCount; ++i)
    {
        if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))))
            return false;

        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(m_rtvDescriptorSize);
    }

    // Create depth buffer.
    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil = { 1.f, 0 };
    CD3DX12_HEAP_PROPERTIES depthHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC depthResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT, width, height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    if (FAILED(m_device->CreateCommittedResource(&depthHeapProperties, D3D12_HEAP_FLAG_NONE, &depthResourceDesc,
                                                 D3D12_RESOURCE_STATE_COMMON, &depthClear, IID_PPV_ARGS(&m_depthBuffer))))
        return false;

    // Create DSV.
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv.Texture2D.MipSlice = 0;
    dsv.Flags = D3D12_DSV_FLAG_NONE;
    m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsv, m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart());

    // Create a readback buffer for the depth.
    m_depthReadbackBuffer = CreateReadbackBuffer(GetTexturePitchBytes(width, sizeof(float)) * height);

    ImGui_ImplDX12_CreateDeviceObjects();

    m_viewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)width, (float)height);
    m_projMat = math::perspectiveFovRH(0.25f * Pif, m_viewport.Width, m_viewport.Height, 0.01f, 1000.f);
    return true;
}

bool Renderer::CreateRootSignature()
{
    // Check root signature 1.1 support...
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = GetRootSignatureFeaturedData();

    // Create a descriptor table for pixel constant buffers
    D3D12_DESCRIPTOR_RANGE1 cbvDescRange = {};
    cbvDescRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    cbvDescRange.NumDescriptors = 1;
    cbvDescRange.BaseShaderRegister = 2;
    cbvDescRange.RegisterSpace = 0;
    cbvDescRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE1 srvDescRange0 = {};
    srvDescRange0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvDescRange0.NumDescriptors = 1;
    srvDescRange0.BaseShaderRegister = 0;
    srvDescRange0.RegisterSpace = 0;
    srvDescRange0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE1 srvDescRange1 = srvDescRange0;
    srvDescRange1.BaseShaderRegister = 1;

    CD3DX12_ROOT_PARAMETER1 rootParams[RootParam::Count];
    rootParams[RootParam::VSSharedConstants].InitAsConstants(sizeof(VSSharedConstants) / 4, 0, 0, D3D12_SHADER_VISIBILITY_ALL); // TODO: tidy up or rename! We're running out of root signature space. They should probably be in a cbuffer instead.
    rootParams[RootParam::PSSharedConstants].InitAsConstants(sizeof(PSSharedConstants) / 4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParams[RootParam::PSConstantBuffer].InitAsDescriptorTable(1, &cbvDescRange, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParams[RootParam::VertexTexture0].InitAsDescriptorTable(1, &srvDescRange0, D3D12_SHADER_VISIBILITY_DOMAIN);
    rootParams[RootParam::Texture0].InitAsDescriptorTable(1, &srvDescRange0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParams[RootParam::Texture1].InitAsDescriptorTable(1, &srvDescRange1, D3D12_SHADER_VISIBILITY_PIXEL);

    // Static sampler for textures.
    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MipLODBias = 0;
    samplerDesc.MaxAnisotropy = 0;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    rootSignatureDesc.Init_1_1((UINT)std::size(rootParams), rootParams, 1, &samplerDesc, rootSignatureFlags);

    ComPtr<ID3DBlob> rootSigBlob;
    ComPtr<ID3DBlob> errBlob;
    if (FAILED(::D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &rootSigBlob, &errBlob)))
    {
        DebugOut("Failed to create root signature: %s\n", errBlob->GetBufferPointer());
        return false;
    }

    if (FAILED(m_device->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
    {
        DebugOut("Failed to create root signature!\n");
        return false;
    }

    return true;
}

bool Renderer::CreateImgui(HWND hwnd)
{
    // Create a separate SRV heap for imgui.
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_imguiSrvDescHeap)) != S_OK)
        return false;

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Setup Platform/Renderer bindings
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(m_device.Get(), BackbufferCount,
                        DXGI_FORMAT_R8G8B8A8_UNORM, m_imguiSrvDescHeap.Get(),
                        m_imguiSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
                        m_imguiSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

    return true;
}

void Renderer::BeginFrame()
{
    ID3D12CommandAllocator* commandAllocator = m_commandAllocators[m_currentBuffer].Get();
    ID3D12Resource* backBuffer = m_renderTargets[m_currentBuffer].Get();

    // Reset command list
    commandAllocator->Reset();
    m_directCommandList->Reset(commandAllocator, nullptr);

    // Transition render target and depth buffer to a renderable state
    CD3DX12_RESOURCE_BARRIER rtBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_directCommandList->ResourceBarrier(1, &rtBarrier);

    CD3DX12_RESOURCE_BARRIER dsBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_depthBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    m_directCommandList->ResourceBarrier(1, &dsBarrier);

    // Clear backbuffer
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart(), m_currentBuffer, m_rtvDescriptorSize);
    float clearColor[] = { 0.3f, 0.65f, 0.99f, 0.0f };
    m_directCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    // Clear depth buffer
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart());
    m_directCommandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

    // Set shared state
    m_directCommandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    m_directCommandList->RSSetViewports(1, &m_viewport);
    m_directCommandList->RSSetScissorRects(1, &m_scissorRect);
    m_directCommandList->SetGraphicsRootSignature(m_rootSignature.Get());

    // Set global uniforms
    VSSharedConstants vertexConstants = { m_viewMat, m_projMat };
    m_directCommandList->SetGraphicsRoot32BitConstants(RootParam::VSSharedConstants, sizeof(VSSharedConstants) / 4, &vertexConstants, 0);

    PSSharedConstants pixelConstants = { Vec3f(math::affineInverse(m_viewMat)[3]) };
    m_directCommandList->SetGraphicsRoot32BitConstants(RootParam::PSSharedConstants, sizeof(PSSharedConstants) / 4, &pixelConstants, 0);

    // Set descriptor heaps for constant buffers.
    ID3D12DescriptorHeap* descriptorHeaps[] = { m_cbvDescHeaps[m_currentBuffer].Get() };
    m_directCommandList->SetDescriptorHeaps(1, descriptorHeaps);
}

void Renderer::EndFrame()
{
    Imgui();

    // Render imgui.
    ID3D12DescriptorHeap* imguiDescHeaps[] = { m_imguiSrvDescHeap.Get() };
    m_directCommandList->SetDescriptorHeaps(1, imguiDescHeaps);
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_directCommandList.Get());

    // Transition render target to present state
    CD3DX12_RESOURCE_BARRIER rtBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_currentBuffer].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_directCommandList->ResourceBarrier(1, &rtBarrier);

    // Transition depth buffer to allow readback
    CD3DX12_RESOURCE_BARRIER dsBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COMMON);
    m_directCommandList->ResourceBarrier(1, &dsBarrier);

    // Read back depth buffer after drawing.
    // TODO: If we used a separate command list (but still m_directCommandQueue),
    // could we allow the frame to present before this has finished, and wait on it separately?
    D3D12_TEXTURE_COPY_LOCATION dst;
    dst.pResource = m_depthReadbackBuffer.Get(),
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint;
    dst.PlacedFootprint.Offset = 0;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_D32_FLOAT;
    dst.PlacedFootprint.Footprint.Width = (UINT)m_viewport.Width;
    dst.PlacedFootprint.Footprint.Height = (UINT)m_viewport.Height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = GetTexturePitchBytes(dst.PlacedFootprint.Footprint.Width, sizeof(float));
    D3D12_TEXTURE_COPY_LOCATION src = { m_depthBuffer.Get() };
    m_directCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    // Submit draw (etc) commands
    m_frameFenceValues[m_currentBuffer] = m_directCommandQueue->Execute(m_directCommandList.Get());

    // Present buffer
    m_swapChain->Present(1, 0);
    m_currentBuffer = m_swapChain->GetCurrentBackBufferIndex();

    // Wait for previous frame's fence
    m_directCommandQueue->WaitFence(m_frameFenceValues[m_currentBuffer]);
}

void Renderer::WaitCurrentFrame()
{
    m_directCommandQueue->WaitFence(m_frameFenceValues[m_currentBuffer ^ 1]);
}

ComPtr<ID3DBlob> Renderer::CompileShader(const wchar_t* filename, ShaderStage stage)
{
    const char* stageTargets[] = {
        "vs_5_1",
        "hs_5_1",
        "ds_5_1",
        "ps_5_1",
    };
    static_assert(std::size(stageTargets) == (size_t)ShaderStage::Count);

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    if (FAILED(::D3DCompileFromFile(filename, nullptr, nullptr, "main", stageTargets[(int)stage], 0, 0, &blob, &error)))
    {
        DebugOut("Failed to load shader '%S':\n\n%s\n\n", filename, error ? error->GetBufferPointer() : "<unknown error>");
        return nullptr;
    }

    return blob;
}

ComPtr<ID3DBlob> Renderer::LoadCompiledShader(const wchar_t* filename)
{
    ComPtr<ID3DBlob> blob;
    if (FAILED(::D3DReadFileToBlob(filename, &blob)))
    {
        DebugOut("Failed to load shader '%S'!\n", filename);
        return nullptr;
    }

    return blob;
}

void Renderer::BeginUploads()
{
    m_copyCommandAllocator->Reset();
    m_copyCommandList->Reset(m_copyCommandAllocator.Get(), nullptr);
}

ComPtr<ID3D12Resource> Renderer::CreateResidentBuffer(size_t size)
{
    ComPtr<ID3D12Resource> ret;
    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
    m_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&ret));
    return ret;
}

ComPtr<ID3D12Resource> Renderer::CreateUploadBuffer(size_t size)
{
    ComPtr<ID3D12Resource> ret;
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
    m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ret));
    return ret;
}

ComPtr<ID3D12Resource> Renderer::CreateReadbackBuffer(size_t size)
{
    ComPtr<ID3D12Resource> ret;
    CD3DX12_HEAP_PROPERTIES readbackHeapProps(D3D12_HEAP_TYPE_READBACK);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
    m_device->CreateCommittedResource(&readbackHeapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&ret));
    return ret;
}

ComPtr<ID3D12Resource> Renderer::CreateConstantBuffer(size_t size)
{
    return CreateUploadBuffer(math::AlignPow2<size_t>(size, CBufferAlignment));
}

void Renderer::CreateBuffer(ComPtr<ID3D12Resource>& bufferOut, ComPtr<ID3D12Resource>& intermediateBuffer, size_t size, const void* data)
{
    // Create destination buffer
    bufferOut = CreateResidentBuffer(size);

    // Create an intermediate buffer to upload via
    // (this must remain in scope in the calling code until the command list has been executed)
    intermediateBuffer = CreateUploadBuffer(size);

    // Upload initial data.
    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = data;
    subresourceData.RowPitch = size;
    subresourceData.SlicePitch = size;
    ::UpdateSubresources<1>(m_copyCommandList.Get(), bufferOut.Get(), intermediateBuffer.Get(), 0, 0, 1, &subresourceData);
}

int Renderer::CreateTexture2D(ComPtr<ID3D12Resource>& textureOut, ComPtr<ID3D12Resource>& intermediateBuffer, size_t width, size_t height, DXGI_FORMAT format)
{
    // Create resident texture.
    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height);
    m_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&textureOut));
    
    // Create upload buffer.
    size_t texelBytes = DirectX::BitsPerPixel(format) >> 3;
    intermediateBuffer = CreateUploadBuffer(width * height * texelBytes);

    // Allocate an SRV.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = resourceDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1; //CountMips(desc.Width, desc.Height);

    for (const ComPtr<ID3D12DescriptorHeap>& heap : m_cbvDescHeaps)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(heap->GetCPUDescriptorHandleForHeapStart(), m_nextCBVDescIndex * m_cbvDescriptorSize);
        m_device->CreateShaderResourceView(textureOut.Get(), &srvDesc, cpuHandle);
    }

    return m_nextCBVDescIndex++;
}

int Renderer::LoadTexture(ComPtr<ID3D12Resource>& textureOut, ComPtr<ID3D12Resource>& intermediateBuffer, const wchar_t* filepath)
{
    // Load the file to a buffer and create the destination texture.
    // NOTE: Loaded with D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS under the assumption
    //       that we will immediately want to generate mips!
    ID3D12Resource* rawTexture = nullptr;
    std::unique_ptr<uint8[]> decodedData;
    std::vector<D3D12_SUBRESOURCE_DATA> subresources = {};
    bool cubemap = false;
    HRESULT ret;
    if (wcscmp(GetFileExtension(filepath), L"dds") == 0)
    {
        ret = DirectX::LoadDDSTextureFromFileEx(m_device.Get(), filepath, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            DirectX::DDS_LOADER_DEFAULT, &rawTexture, decodedData, subresources, nullptr, &cubemap);
    }
    else
    {
        subresources.resize(1);
        ret = DirectX::LoadWICTextureFromFileEx(m_device.Get(), filepath, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                                DirectX::WIC_LOADER_MIP_RESERVE, &rawTexture, decodedData, subresources.front());
    }
    Assert(SUCCEEDED(ret));
    Assert(0 < subresources.size() && subresources.size() <= 6);

    if (SUCCEEDED(ret))
    {
        // Retrieve texture info. Would be nice if DirectXTex returned this, but alas.
        const D3D12_RESOURCE_DESC& desc = rawTexture->GetDesc();

        // Create intermediate buffer and fill it in.
        // The resource will automatically transition to D3D12_RESOURCE_STATE_COMMON
        // when it is used on a direct command list, but for now, calling code should 
        // then transition to D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE for performance.
        // Could manage that internally to the renderer if we wanted.
        intermediateBuffer = CreateUploadBuffer(subresources.size() * subresources.front().SlicePitch);
        ::UpdateSubresources<6>(m_copyCommandList.Get(), rawTexture, intermediateBuffer.Get(), 0, 0, subresources.size(), subresources.data());

        // Allocate an SRV.
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = desc.Format;
        if (cubemap)
        {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MipLevels = 1;
        }
        else 
        {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = CountMips(desc.Width, desc.Height);
        }

        for (const ComPtr<ID3D12DescriptorHeap>& heap : m_cbvDescHeaps)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(heap->GetCPUDescriptorHandleForHeapStart(), m_nextCBVDescIndex * m_cbvDescriptorSize);
            m_device->CreateShaderResourceView(rawTexture, &srvDesc, cpuHandle);
        }

        textureOut = rawTexture;
        return m_nextCBVDescIndex++;
    }

    return -1;
}

UINT64 Renderer::EndUploads()
{
    return m_copyCommandQueue->Execute(m_copyCommandList.Get());
}

void Renderer::WaitUploads(UINT64 fenceVal)
{
    m_copyCommandQueue->WaitFence(fenceVal);
}

void Renderer::GenerateMips(ID3D12Resource* texture)
{
    BeginCompute();
    m_genMips->Compute(*this, texture);
    UINT64 fenceVal = EndCompute();
    WaitCompute(fenceVal);
}

void Renderer::BindDescriptor(int descIndex, RootParam::E slot)
{
    Assert(descIndex < m_nextCBVDescIndex);
    Assert(slot == RootParam::PSConstantBuffer || (RootParam::VertexTexture0 <= slot && slot <= RootParam::Texture1));

    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_cbvDescHeaps[m_currentBuffer]->GetGPUDescriptorHandleForHeapStart(), descIndex * m_cbvDescriptorSize);
    m_directCommandList->SetGraphicsRootDescriptorTable(slot, gpuHandle);
}

void Renderer::BindComputeDescriptor(int descIndex, int slot)
{
    Assert(descIndex < m_nextComputeDescIndex);

    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_computeDescHeap->GetGPUDescriptorHandleForHeapStart(), descIndex * m_cbvDescriptorSize);
    m_computeCommandList->SetComputeRootDescriptorTable(slot, gpuHandle);
}

void Renderer::BeginCompute()
{
    Assert(m_nextComputeDescIndex == 0); // Another compute already in progress?

    // Reset command list.
    m_computeCommandAllocator->Reset();
    m_computeCommandList->Reset(m_computeCommandAllocator.Get(), nullptr);

    // Set descriptor heap.
    ID3D12DescriptorHeap* descriptorHeaps[] = { m_computeDescHeap.Get() };
    m_computeCommandList->SetDescriptorHeaps(1, descriptorHeaps);
}

UINT64 Renderer::EndCompute()
{
    return m_computeCommandQueue->Execute(m_computeCommandList.Get());
}

void Renderer::WaitCompute(UINT64 fenceVal)
{
    m_computeCommandQueue->WaitFence(fenceVal);

    // "Free" allocated compute descriptors.
    m_nextComputeDescIndex = 0;
}

int Renderer::AllocateConstantBufferViews(ID3D12Resource* (&buffers)[BackbufferCount], UINT size)
{
    Assert(m_nextCBVDescIndex < NumCBVDescriptors);
    size = math::AlignPow2<UINT>(size, CBufferAlignment);

    // Allocate descriptors for both frames.
    for (int i = 0; i < BackbufferCount; ++i)
    {
        D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {};
        desc.BufferLocation = buffers[i]->GetGPUVirtualAddress();
        desc.SizeInBytes = size;
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_cbvDescHeaps[i]->GetCPUDescriptorHandleForHeapStart(), m_nextCBVDescIndex * m_cbvDescriptorSize);
        m_device->CreateConstantBufferView(&desc, cpuHandle);
    }

    return m_nextCBVDescIndex++;
}

void Renderer::FreeConstantBufferView(int index)
{
    Assert(index + 1 == m_nextCBVDescIndex);
    --m_nextCBVDescIndex;
}

int Renderer::AllocateComputeUAV(ID3D12Resource* targetResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc)
{
    Assert(m_nextComputeDescIndex < NumComputeDescriptors);
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_computeDescHeap->GetCPUDescriptorHandleForHeapStart(), m_nextComputeDescIndex * m_cbvDescriptorSize);
    m_device->CreateUnorderedAccessView(targetResource, nullptr, &desc, cpuHandle);
    return m_nextComputeDescIndex++;
}

int Renderer::AllocateComputeSRV(ID3D12Resource* targetResource, const D3D12_SHADER_RESOURCE_VIEW_DESC& desc)
{
    Assert(m_nextComputeDescIndex < NumComputeDescriptors);
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_computeDescHeap->GetCPUDescriptorHandleForHeapStart(), m_nextComputeDescIndex * m_cbvDescriptorSize);
    m_device->CreateShaderResourceView(targetResource, &desc, cpuHandle);
    return m_nextComputeDescIndex++;
}

D3D12_FEATURE_DATA_ROOT_SIGNATURE Renderer::GetRootSignatureFeaturedData() const
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }
    return featureData;
}

float Renderer::ReadDepth(int x, int y)
{
    Assert(0 <= x && x < (int)m_viewport.Width);
    Assert(0 <= y && y < (int)m_viewport.Height);

    // Make sure we've finished reading back the last frame's depth buffer.
    WaitCurrentFrame();

    int pitch = GetTexturePitchBytes((int)m_viewport.Width, sizeof(float)) / sizeof(float);
    int index = pitch * y + x;

    // Map readback buffer and return the value.
    const float* depthData = nullptr;
    D3D12_RANGE readRange = { index * sizeof(float), (index + 1) * sizeof(float) };
    m_depthReadbackBuffer->Map(0, &readRange, (void**)&depthData);
    Assert(depthData);

    float depth = depthData[index];

    D3D12_RANGE writeRange = {};
    m_depthReadbackBuffer->Unmap(0, &writeRange);

    return depth;
}

Vec3f Renderer::Unproject(Vec3f screenCoords) const
{
    Assert(0.f <= screenCoords.z && screenCoords.z <= 1.f);

    // Normalise X and Y to [0, 1]
    screenCoords.x /= m_viewport.Width;
    screenCoords.y /= m_viewport.Height;

    // Flip so Y is up
    screenCoords.y = 1.f - screenCoords.y;

    // Normalise X and Y to [-1, 1]
    screenCoords.x = 2.f * screenCoords.x - 1.f;
    screenCoords.y = 2.f * screenCoords.y - 1.f;

    // Unproject.
    // Note: this is left in the nonlinear space used for projection, so
    // e.g. screenCoords.z == 0.5 will NOT give the half way point between 
    // the near and far planes. This matches up with values in the depth buffer.
    Vec4f viewCoords = math::inverse(m_projMat) * Vec4f(screenCoords, 1.f);
    return Vec3f(viewCoords) / viewCoords.w;
}

void Renderer::BeginImguiFrame()
{
    // Do stuff that needs to happen at the start of the update, not start of the render.
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Renderer::Imgui()
{
    ImGui::Begin("Renderer");
    ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::End();
}

}

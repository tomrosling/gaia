#include "Renderer.hpp"
#include <DirectXTex/DirectXTex.h>
#include <DDSTextureLoader/DDSTextureLoader12.h>
#include <WICTextureLoader/WICTextureLoader12.h>
#include <examples/imgui_impl_win32.h>
#include <examples/imgui_impl_dx12.h>
#include "Math/GaiaMath.hpp"
#include "Math/AABB.hpp"
#include "Math/Plane.hpp"
#include "File.hpp"
#include "CommandQueue.hpp"
#include "GenerateMips.hpp"
#include "UploadManager.hpp"

#include "DebugDraw.hpp"

namespace gaia
{

static constexpr int CBufferAlignment = 256;
static constexpr int NumCBVDescriptors = 32;
static constexpr int NumComputeDescriptors = 64;
static constexpr int NumSamplers = 1;

static constexpr int SunShadowmapSize = 4096;

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
    Mat4f mvpMat;
    Mat4f shadowMvpMat;
};

struct PSSharedConstants
{
    Vec3f camPos;
    float pad1;
    Vec3f sunDirection;
    float pad2;
};

namespace StaticSampler
{
enum E
{
    Basic,
    Shadowmap,
    Count
};
}


// Debug state.
// TODO: Move this to its own file and improve interface.
struct RendererDebugState
{
    bool m_freezeCascades = false;
    bool m_drawShadowBounds = false;
    AABB3f m_frozenShadowBounds = AABB3fInvalid;
};

static RendererDebugState s_debugState;




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

    // Create DSV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 2;
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

    // Create sampler desc heap.
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.NumDescriptors = NumSamplers;
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_samplerDescHeap))))
        return false;

    // Create descriptor heap for compute shaders.
    D3D12_DESCRIPTOR_HEAP_DESC computeHeapDesc = {};
    computeHeapDesc.NumDescriptors = NumComputeDescriptors;
    computeHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    computeHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_device->CreateDescriptorHeap(&computeHeapDesc, IID_PPV_ARGS(&m_computeDescHeap))))
        return false;

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    m_cbvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_samplerDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    // Create stats query resources.
    D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
    queryHeapDesc.Count = 1;
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
    if (FAILED(m_device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_statsQueryHeap))))
        return false;

    for (auto& buf : m_statsQueryBuffers)
    {
        buf = CreateReadbackBuffer(sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
    }

    // Create constant buffers
    CreateMappedConstantBuffer(m_vsSharedConstants);
    CreateMappedConstantBuffer(m_vsSharedConstantsShadowPass);

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

#ifdef _DEBUG
    m_directCommandList->SetName(L"DirectCommandList");
    m_copyCommandList->SetName(L"CopyCommandList");
    m_computeCommandList->SetName(L"ComputeCommandList");
#endif

    // NOTE: We don't actually create the render and depth targets here, assuming ResizeViewport will be called with an appropriate size.

    if (!CreateRootSignature())
        return false;

    m_uploadManager = std::make_unique<UploadManager>();

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

    // Create sun shadows depth buffer
    CD3DX12_RESOURCE_DESC sunShadowDepthResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_D32_FLOAT, SunShadowmapSize, SunShadowmapSize, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    if (FAILED(m_device->CreateCommittedResource(&depthHeapProperties, D3D12_HEAP_FLAG_NONE, &sunShadowDepthResourceDesc,
                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &depthClear, IID_PPV_ARGS(&m_sunShadowDepthBuffer))))
        return false;

    ID3D12Resource* sunShadowMapTex[] = { m_sunShadowDepthBuffer.Get() };
    m_sunShadowmapDescIndex = AllocateTex2DSRVs(1, sunShadowMapTex, DXGI_FORMAT_R32_FLOAT);

    // Create DSVs.
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvDesc, GetMainDSV());
    m_device->CreateDepthStencilView(m_sunShadowDepthBuffer.Get(), &dsvDesc, GetSunShadowDSV());

#ifdef _DEBUG
    m_depthBuffer->SetName(L"Main Depth Buffer");
    m_sunShadowDepthBuffer->SetName(L"Sun Shadowmap");
#endif

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

    // Create descriptor tables
    D3D12_DESCRIPTOR_RANGE1 cbvDescRange = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 2);
    D3D12_DESCRIPTOR_RANGE1 vertexTexture0SrvDescRange = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0);
    D3D12_DESCRIPTOR_RANGE1 vertexTexture1SrvDescRange = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 8);
    D3D12_DESCRIPTOR_RANGE1 srvDescRange0 = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    D3D12_DESCRIPTOR_RANGE1 srvDescRange1 = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    D3D12_DESCRIPTOR_RANGE1 srvDescRange2 = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    D3D12_DESCRIPTOR_RANGE1 srvDescRange3 = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
    D3D12_DESCRIPTOR_RANGE1 sunShadowMapDescRange = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
    D3D12_DESCRIPTOR_RANGE1 samplerSrvDescRange = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, StaticSampler::Count);

    CD3DX12_ROOT_PARAMETER1 rootParams[RootParam::Count];
    rootParams[RootParam::VSSharedConstants].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE, D3D12_SHADER_VISIBILITY_ALL); // TODO: Rename!
    rootParams[RootParam::PSSharedConstants].InitAsConstants(sizeof(PSSharedConstants) / 4, 1, 0, D3D12_SHADER_VISIBILITY_ALL); //
    rootParams[RootParam::PSConstantBuffer].InitAsDescriptorTable(1, &cbvDescRange, D3D12_SHADER_VISIBILITY_ALL); // TODO: Rename!
    rootParams[RootParam::VertexTexture0].InitAsDescriptorTable(1, &vertexTexture0SrvDescRange, D3D12_SHADER_VISIBILITY_DOMAIN);
    rootParams[RootParam::VertexTexture1].InitAsDescriptorTable(1, &vertexTexture1SrvDescRange, D3D12_SHADER_VISIBILITY_DOMAIN);
    rootParams[RootParam::Texture0].InitAsDescriptorTable(1, &srvDescRange0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParams[RootParam::Texture1].InitAsDescriptorTable(1, &srvDescRange1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParams[RootParam::Texture2].InitAsDescriptorTable(1, &srvDescRange2, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParams[RootParam::Texture3].InitAsDescriptorTable(1, &srvDescRange3, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParams[RootParam::SunShadowMap].InitAsDescriptorTable(1, &sunShadowMapDescRange, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParams[RootParam::Sampler0].InitAsDescriptorTable(1, &samplerSrvDescRange, D3D12_SHADER_VISIBILITY_DOMAIN);

    // Static sampler for textures.
    CD3DX12_STATIC_SAMPLER_DESC staticSamplers[StaticSampler::Count];
    staticSamplers[StaticSampler::Basic].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    staticSamplers[StaticSampler::Basic].MaxAnisotropy = 0;
    staticSamplers[StaticSampler::Shadowmap].Init(1, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR);
    staticSamplers[StaticSampler::Shadowmap].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[StaticSampler::Shadowmap].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    staticSamplers[StaticSampler::Shadowmap].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    rootSignatureDesc.Init_1_1(RootParam::Count, rootParams, StaticSampler::Count, staticSamplers, rootSignatureFlags);

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

    // Wait/clear pending uploads.
    m_uploadManager->BeginFrame(*m_copyCommandQueue);

    // Reset command list
    commandAllocator->Reset();
    m_directCommandList->Reset(commandAllocator, nullptr);

    // Start tracking stats for the frame.
    m_directCommandList->BeginQuery(m_statsQueryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);

    // Transition render target and depth buffer to a renderable state
    ID3D12Resource* backBuffer = m_renderTargets[m_currentBuffer].Get();
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

    // Clear depth buffers
    m_directCommandList->ClearDepthStencilView(GetMainDSV(), D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

    // Set descriptor heaps.
    ID3D12DescriptorHeap* descriptorHeaps[] = { m_cbvDescHeaps[m_currentBuffer].Get(), m_samplerDescHeap.Get() };
    m_directCommandList->SetDescriptorHeaps((int)std::size(descriptorHeaps), descriptorHeaps);

    // Clear scissor state and set root signature
    D3D12_RECT scissorRect = { 0, 0, LONG_MAX, LONG_MAX };
    m_directCommandList->RSSetScissorRects(1, &scissorRect);
    m_directCommandList->SetGraphicsRootSignature(m_rootSignature.Get());

    // Set global constants
    PSSharedConstants pixelConstants = { Vec3f(math::affineInverse(m_viewMat)[3]), 0.f, m_sunDirection, 0.f };
    m_directCommandList->SetGraphicsRoot32BitConstants(RootParam::PSSharedConstants, sizeof(PSSharedConstants) / 4, &pixelConstants, 0);
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
    dst.PlacedFootprint.Offset = 0;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_D32_FLOAT;
    dst.PlacedFootprint.Footprint.Width = (UINT)m_viewport.Width;
    dst.PlacedFootprint.Footprint.Height = (UINT)m_viewport.Height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = GetTexturePitchBytes(dst.PlacedFootprint.Footprint.Width, sizeof(float));
    D3D12_TEXTURE_COPY_LOCATION src = { m_depthBuffer.Get() };
    m_directCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    // Get stats query data.
    m_directCommandList->EndQuery(m_statsQueryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    m_directCommandList->ResolveQueryData(m_statsQueryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1, m_statsQueryBuffers[m_currentBuffer].Get(), 0);

    // Submit draw (etc) commands
    m_frameFenceValues[m_currentBuffer] = m_directCommandQueue->Execute(m_directCommandList.Get());

    // Present buffer
    m_swapChain->Present(m_vsync ? 1 : 0, 0);
    m_currentBuffer = m_swapChain->GetCurrentBackBufferIndex();

    // Wait for previous frame's fence
    m_directCommandQueue->WaitFence(m_frameFenceValues[m_currentBuffer]);
}

void Renderer::BeginShadowPass()
{
    // Transition depth buffer to a renderable state
    CD3DX12_RESOURCE_BARRIER dsBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_sunShadowDepthBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    m_directCommandList->ResourceBarrier(1, &dsBarrier);

    // Clear depth buffer
    m_directCommandList->ClearDepthStencilView(GetSunShadowDSV(), D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

    // Set render target and viewport
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetSunShadowDSV();
    m_directCommandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    CD3DX12_VIEWPORT viewport(0.f, 0.f, (float)SunShadowmapSize, (float)SunShadowmapSize);
    m_directCommandList->RSSetViewports(1, &viewport);

    // Set VSSharedConstants (matrices) buffer for this pass
    VSSharedConstants& constants = *m_vsSharedConstantsShadowPass.GetMappedData(m_currentBuffer);
    const auto& [viewMat, projMat] = GetSunShadowMatrices();
    constants.viewMat = viewMat;
    constants.projMat = projMat;
    constants.mvpMat = projMat * viewMat; // Note: no model matrix for now
    constants.shadowMvpMat = Mat4fIdentity;
    m_directCommandList->SetGraphicsRootConstantBufferView(RootParam::VSSharedConstants, m_vsSharedConstantsShadowPass.GetBufferGPUVirtualAddress(m_currentBuffer));
}

void Renderer::EndShadowPass()
{
    // Transition depth buffer to allow lighting to read it
    CD3DX12_RESOURCE_BARRIER dsBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_sunShadowDepthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_directCommandList->ResourceBarrier(1, &dsBarrier);
}

void Renderer::BeginGeometryPass()
{
    // Set shared state
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart(), m_currentBuffer, m_rtvDescriptorSize);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetMainDSV();
    m_directCommandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    m_directCommandList->RSSetViewports(1, &m_viewport);

    // Set VSSharedConstants (matrices) buffer for this pass
    VSSharedConstants& constants = *m_vsSharedConstants.GetMappedData(m_currentBuffer);
    constants.viewMat = m_viewMat;
    constants.projMat = m_projMat;
    constants.mvpMat = m_projMat * m_viewMat; // Note: no model matrix for now
    const auto& [shadowView, shadowProj] = GetSunShadowMatrices();
    constants.shadowMvpMat = shadowProj * shadowView;
    m_directCommandList->SetGraphicsRootConstantBufferView(RootParam::VSSharedConstants, m_vsSharedConstants.GetBufferGPUVirtualAddress(m_currentBuffer));

    // Bind sun shadow map
    BindDescriptor(m_sunShadowmapDescIndex, RootParam::SunShadowMap);
}

void Renderer::EndGeometryPass()
{
    // Do nothing for now
}

void Renderer::WaitCurrentFrame()
{
    m_directCommandQueue->WaitFence(m_frameFenceValues[m_currentBuffer ^ 1]);
}

ComPtr<ID3DBlob> Renderer::CompileShader(const wchar_t* filename, ShaderStage stage)
{
    class ShaderIncludeHandler : public ID3DInclude
    {
    public:
        HRESULT Open(D3D_INCLUDE_TYPE includeType, LPCSTR filename, LPCVOID parentData, LPCVOID* data, UINT* bytes) override
        {
            // Open include files relative the application's working directory.
            File file;
            if (!file.Open(filename, EFileOpenMode::Read))
            {
                *data = nullptr;
                *bytes = 0;
                return E_INVALIDARG;
            }
            
            // Read the file into a buffer to return to the compiler
            int length = file.GetLength();
            char* charData = new char[length + 1];
            file.Read(charData, length);
            charData[length] = '\0';
            *data = charData;
            *bytes = (UINT)(length + 1);

            return S_OK;
        }

        HRESULT Close(LPCVOID data) override
        {
            delete[] (char*)data;
            return S_OK;
        }
    };

    const char* stageTargets[] = {
        "vs_5_1",
        "hs_5_1",
        "ds_5_1",
        "ps_5_1",
    };
    static_assert(std::size(stageTargets) == (size_t)ShaderStage::Count);

    ShaderIncludeHandler includeHandler;
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    if (FAILED(::D3DCompileFromFile(filename, nullptr, &includeHandler, "main", stageTargets[(int)stage], D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &blob, &error)))
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

ComPtr<ID3D12PipelineState> Renderer::CreateComputePipelineState(const wchar_t* shaderFilename, ID3D12RootSignature& rootSignature)
{
    // Load shader.
    ComPtr<ID3DBlob> shader = LoadCompiledShader(shaderFilename);
    if (!shader)
        return nullptr;

    // Create PSO.
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_CS computeShader;
    };

    PipelineStateStream stream;
    stream.rootSignature = &rootSignature;
    stream.computeShader = CD3DX12_SHADER_BYTECODE(shader.Get());

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
        sizeof(PipelineStateStream), &stream
    };

    ComPtr<ID3D12PipelineState> pipelineState;
    m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&pipelineState));
    return pipelineState;
}

VertexBuffer Renderer::CreateVertexBuffer(const Span<const uchar>& vertexData, int vertexStride)
{
    VertexBuffer ret;
    ret.buffer = CreateBuffer(vertexData.Size(), vertexData.Data());
    ret.view.BufferLocation = ret.buffer->GetGPUVirtualAddress();
    ret.view.SizeInBytes = vertexData.Size();
    ret.view.StrideInBytes = vertexStride;
    return ret;
}

IndexBuffer Renderer::CreateIndexBuffer(const Span<const uchar>& indexData, DXGI_FORMAT format)
{
    Assert(format == DXGI_FORMAT_R8_UINT || format == DXGI_FORMAT_R16_UINT || format == DXGI_FORMAT_R32_UINT);

    IndexBuffer ret;
    ret.buffer = CreateBuffer(indexData.Size(), indexData.Data());
    ret.view.BufferLocation = ret.buffer->GetGPUVirtualAddress();
    ret.view.SizeInBytes = indexData.Size();
    ret.view.Format = DXGI_FORMAT_R16_UINT;
    return ret;
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
    return CreateUploadBuffer(math::RoundUpPow2<size_t>(size, CBufferAlignment));
}

ComPtr<ID3D12Resource> Renderer::CreateBuffer(size_t size, const void* data)
{
    // Create destination and upload buffer.
    ID3D12Resource* uploadBuffer = nullptr;
    ComPtr<ID3D12Resource> residentBuffer = CreateBuffer(uploadBuffer, size);

    // Upload initial data.
    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = data;
    subresourceData.RowPitch = size;
    subresourceData.SlicePitch = size;
    ::UpdateSubresources<1>(m_copyCommandList.Get(), residentBuffer.Get(), uploadBuffer, 0, 0, 1, &subresourceData);

    return residentBuffer;
}

ComPtr<ID3D12Resource> Renderer::CreateBuffer(ID3D12Resource*& outUploadBuffer, size_t size)
{
    // Create destination buffer
    ComPtr<ID3D12Resource> residentBuffer = CreateResidentBuffer(size);

    // Create an intermediate buffer to upload via
    ComPtr<ID3D12Resource> uploadBuffer = CreateUploadBuffer(size);
    m_uploadManager->AddIntermediateResource(uploadBuffer.Get());
    outUploadBuffer = uploadBuffer.Get();

    return residentBuffer;
}

ComPtr<ID3D12Resource> Renderer::CreateTexture2D(const Texture2DParams& params)
{
    // Create resident texture.
    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(params.format, params.width, params.height);
    resourceDesc.MipLevels = 1; //CountMips(width, height);
    resourceDesc.Flags = params.flags;
    ComPtr<ID3D12Resource> texture;
    m_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                      params.initialState, nullptr, IID_PPV_ARGS(&texture));
    
#ifdef _DEBUG
    texture->SetName(params.name);
#endif

    return texture;
}

ComPtr<ID3D12Resource> Renderer::CreateTexture2DUploadBuffer(const Texture2DParams& params)
{
    // Create upload buffer.
    size_t texelBytes = GetFormatSize(params.format);
    ComPtr<ID3D12Resource> uploadBuffer = CreateUploadBuffer(params.width * params.height * texelBytes);

#ifdef _DEBUG
    std::wstring uploadBufferName = std::wstring(params.name) + L" intermediate buffer";
    uploadBuffer->SetName(uploadBufferName.c_str());
#endif

    return uploadBuffer;
}

int Renderer::AllocateTex2DSRVs(int count, ID3D12Resource** textures, DXGI_FORMAT format)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1; //CountMips(desc.Width, desc.Height);

    Assert(m_nextCBVDescIndex + count <= NumCBVDescriptors);
    int ret = m_nextCBVDescIndex;
    m_nextCBVDescIndex += count;

    for (int i = 0; i < count; ++i)
    {
        for (const ComPtr<ID3D12DescriptorHeap>& heap : m_cbvDescHeaps)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(heap->GetCPUDescriptorHandleForHeapStart(), (ret + i) * m_cbvDescriptorSize);
            m_device->CreateShaderResourceView(textures[i], &srvDesc, cpuHandle);
        }
    }

    return ret;
}

void Renderer::FreeSRVs(int index, int count)
{
    Assert(index + count == m_nextCBVDescIndex);
    m_nextCBVDescIndex -= count;
}

int Renderer::LoadTexture(ComPtr<ID3D12Resource>& textureOut, const wchar_t* filepath, bool loadMips)
{
    // Load the file to a buffer and create the destination texture.
    // NOTE: Loaded with D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS under the assumption
    //       that we will immediately want to generate mips!
    ID3D12Resource* rawTexture = nullptr;
    std::unique_ptr<uint8[]> decodedData;
    std::vector<D3D12_SUBRESOURCE_DATA> subresources = {};
    bool cubemap = false;
    D3D12_RESOURCE_FLAGS resourceFlags = loadMips ? D3D12_RESOURCE_FLAG_NONE : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HRESULT ret;
    if (wcscmp(GetFileExtension(filepath), L"dds") == 0)
    {
        ret = DirectX::LoadDDSTextureFromFileEx(m_device.Get(), filepath, 0, resourceFlags,
            DirectX::DDS_LOADER_DEFAULT, &rawTexture, decodedData, subresources, nullptr, &cubemap);
    }
    else
    {
        subresources.resize(1);
        ret = DirectX::LoadWICTextureFromFileEx(m_device.Get(), filepath, 0, resourceFlags,
                                                DirectX::WIC_LOADER_MIP_RESERVE | DirectX::WIC_LOADER_IGNORE_SRGB, &rawTexture, decodedData, subresources.front());
    }

    const int MaxSubresources = 16;
    Assert(SUCCEEDED(ret));
    Assert(0 < subresources.size() && subresources.size() <= MaxSubresources);

    if (SUCCEEDED(ret))
    {
        // Retrieve texture info. Would be nice if DirectXTex returned this, but alas.
        const D3D12_RESOURCE_DESC& desc = rawTexture->GetDesc();

        // Create intermediate buffer and fill it in.
        // The resource will automatically transition to D3D12_RESOURCE_STATE_COMMON
        // when it is used on a direct command list, but for now, calling code should 
        // then transition to D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE for performance.
        // Could manage that internally to the renderer if we wanted.
        size_t bufferSize = std::accumulate(subresources.begin(), subresources.end(), (size_t)0, [](size_t lhs, const D3D12_SUBRESOURCE_DATA& rhs) { return lhs + rhs.SlicePitch; });
        ComPtr<ID3D12Resource> intermediateBuffer = CreateUploadBuffer(subresources.size() * subresources.front().SlicePitch);
        m_uploadManager->AddIntermediateResource(intermediateBuffer.Get());
        ::UpdateSubresources<MaxSubresources>(m_copyCommandList.Get(), rawTexture, intermediateBuffer.Get(), 0, 0, subresources.size(), subresources.data());

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
            srvDesc.Texture2D.MipLevels = loadMips ? subresources.size() : CountMips(desc.Width, desc.Height);
        }

        for (const ComPtr<ID3D12DescriptorHeap>& heap : m_cbvDescHeaps)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(heap->GetCPUDescriptorHandleForHeapStart(), m_nextCBVDescIndex * m_cbvDescriptorSize);
            m_device->CreateShaderResourceView(rawTexture, &srvDesc, cpuHandle);
        }

#ifdef _DEBUG
        rawTexture->SetName(filepath);
        intermediateBuffer->SetName((std::wstring(filepath) + L" intermediate buffer").c_str());
#endif

        textureOut = rawTexture;
        return m_nextCBVDescIndex++;
    }

    return -1;
}

UINT64 Renderer::EndUploads()
{
    UINT64 fenceValue = m_copyCommandQueue->Execute(m_copyCommandList.Get());
    m_uploadManager->SetFenceValue(fenceValue);
    return fenceValue;
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
    Assert(slot >= RootParam::VSSharedConstants || (RootParam::VertexTexture0 <= slot && slot <= RootParam::Texture3));

    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_cbvDescHeaps[m_currentBuffer]->GetGPUDescriptorHandleForHeapStart(), descIndex * m_cbvDescriptorSize);
    m_directCommandList->SetGraphicsRootDescriptorTable(slot, gpuHandle);
}

void Renderer::BindSampler(int descIndex)
{
    Assert(descIndex < m_nextSamplerIndex);

    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle(m_samplerDescHeap->GetGPUDescriptorHandleForHeapStart(), descIndex * m_samplerDescriptorSize);
    m_directCommandList->SetGraphicsRootDescriptorTable(RootParam::Sampler0, gpuHandle);
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
    size = math::RoundUpPow2<UINT>(size, CBufferAlignment);

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

int Renderer::AllocateSampler(const D3D12_SAMPLER_DESC& desc)
{
    Assert(m_nextSamplerIndex < NumSamplers);

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_samplerDescHeap->GetCPUDescriptorHandleForHeapStart(), m_nextSamplerIndex * m_samplerDescriptorSize);
    m_device->CreateSampler(&desc, cpuHandle);

    return m_nextSamplerIndex++;
}

void Renderer::FreeSampler(int index)
{
    Assert(index + 1 == m_nextSamplerIndex);
    --m_nextSamplerIndex;
}

int Renderer::AllocateComputeUAV(ID3D12Resource* targetResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC& desc)
{
    Assert(m_nextComputeDescIndex < NumComputeDescriptors);
    Assert(!targetResource || targetResource->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
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
    if (ImGui::Begin("Renderer"))
    {
        ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        Vec3f camPos = Vec3f(math::affineInverse(m_viewMat)[3]);
        ImGui::Text("Cam Pos: (%.2f, %.2f, %.2f)", camPos.x, camPos.y, camPos.z);
        ImGui::Checkbox("VSync", &m_vsync);

        float sunAltitude = acosf(m_sunDirection.y);
        float sunAzimuth = atan2f(m_sunDirection.x, m_sunDirection.z);
        bool azimuthChanged = ImGui::SliderFloat("Sun Azimuth", &sunAzimuth, -Pif, Pif);
        bool altitudeChanged = ImGui::SliderFloat("Sun Altitude", &sunAltitude, 0.f, Pif);
        if (azimuthChanged || altitudeChanged)
        {
            m_sunDirection = Vec3f(sinf(sunAltitude) * sinf(sunAzimuth), cosf(sunAltitude), sinf(sunAltitude) * cosf(sunAzimuth));
        }

        if (ImGui::CollapsingHeader("Sun Shadows"))
        {
            ImGui::Checkbox("Draw Bounds", &s_debugState.m_drawShadowBounds);
            if (ImGui::Checkbox("Freeze Cascades", &s_debugState.m_freezeCascades))
            {
                s_debugState.m_frozenShadowBounds = AABB3fInvalid;
            }
        }

        if (ImGui::CollapsingHeader("Stats (Direct Command List Only)"))
        {
            // Read stats out of the previous frame's buffer.
            const D3D12_QUERY_DATA_PIPELINE_STATISTICS* stats = nullptr;
            D3D12_RANGE readRange = { 0, sizeof(*stats) };
            m_statsQueryBuffers[m_currentBuffer]->Map(0, &readRange, (void**)&stats);
            Assert(stats);

            ImGui::Text("IAVertices:    %llu", stats->IAVertices);
            ImGui::Text("IAPrimitives:  %llu", stats->IAPrimitives);
            ImGui::Text("VSInvocations: %llu", stats->VSInvocations);
            ImGui::Text("GSInvocations: %llu", stats->GSInvocations);
            ImGui::Text("GSPrimitives:  %llu", stats->GSPrimitives);
            ImGui::Text("CInvocations:  %llu", stats->CInvocations);
            ImGui::Text("CPrimitives:   %llu", stats->CPrimitives);
            ImGui::Text("PSInvocations: %llu", stats->PSInvocations);
            ImGui::Text("HSInvocations: %llu", stats->HSInvocations);
            ImGui::Text("DSInvocations: %llu", stats->DSInvocations);
            ImGui::Text("CSInvocations: %llu", stats->CSInvocations);

            D3D12_RANGE writeRange = {};
            m_statsQueryBuffers[m_currentBuffer]->Unmap(0, &writeRange);
        }
    }
    ImGui::End();
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::GetMainDSV()
{
    return m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::GetSunShadowDSV()
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart(), 1, m_dsvDescriptorSize);
}

Pair<Mat4f, Mat4f> Renderer::GetSunShadowMatrices() const 
{
    // Transform world shadow bounds into view space.
    Mat4f sunViewMatrix = math::lookAtRH(Vec3fZero, m_sunDirection, fabsf(m_sunDirection.y) < 0.999f ? Vec3fY : Vec3fZ);

    // Get AABB of the area we want to cast and receive shadows over.
    AABB3f shadowBounds = GetShadowBounds();

    // Debug option to view shadow bounds from different angles.
    if (s_debugState.m_freezeCascades)
    {
        if (s_debugState.m_frozenShadowBounds.IsValid())
        {
            shadowBounds = s_debugState.m_frozenShadowBounds;
        }
        else
        {
            s_debugState.m_frozenShadowBounds = shadowBounds;
        }
    }

    // Transform AABB and recalculate bounds in shadow space.
    AABB3f shadowSpaceBounds = shadowBounds.AffineTransformed(sunViewMatrix);

    // Increase z bounds to allow geometry off-camera which should still cast shadows.
    shadowSpaceBounds.m_min.z -= 100.f;
    
    // Debug draw.
    if (s_debugState.m_drawShadowBounds)
    {
        DebugDraw::Instance().DrawAABB3f(shadowBounds, Vec4u8(0xff, 0x00, 0x00, 0xff));
        DebugDraw::Instance().DrawAABB3f(shadowSpaceBounds, Vec4u8(0x00, 0xff, 0x00, 0xff), math::inverse(sunViewMatrix));
    }
    
    // Create an ortho projection matrix out of the bounds.
    // Negate and swap min/max Z to account for right-handed world/view space. 
    // In view space, +Z goes back from the camera,  whereas in clip space it goes in front of it.
    // Hence, the "max" of the view box in view space is actually the near plane, and the "min" is the far plane.
    Mat4f proj = math::orthoRH(shadowSpaceBounds.m_min.x, shadowSpaceBounds.m_max.x, shadowSpaceBounds.m_min.y, shadowSpaceBounds.m_max.y, -shadowSpaceBounds.m_max.z, -shadowSpaceBounds.m_min.z);

    return { sunViewMatrix, proj };
}

AABB3f Renderer::GetShadowBounds() const
{
    // Get view frustum corners in world space
    Mat4f invViewProjMat = math::inverse(m_projMat * m_viewMat);
    Vec3f frustumCorners[8];
    for (int z = 0; z < 2; ++z)
    {
        for (int y = 0; y < 2; ++y)
        {
            for (int x = 0; x < 2; ++x)
            {
                // Note: x,y are [-1, 1], z is [0,1]
                int i = z * 4 + y * 2 + x;
                Vec4f point = invViewProjMat * Vec4f(2.f * (float)x - 1.f, 2.f * (float)y - 1.f, (float)z, 1.f);
                frustumCorners[i] = Vec3f(point) / point.w;
            }
        }
    }

    // TODO: Get this from a scenegraph or similar
    AABB3f sceneBounds{ Vec3f(-450.f, -30.f, -450.f), Vec3f(450.f, 30.f, 450.f) };

    // Cast a ray from the camera to each point and intersect with the lower bounding plane of the scene.
    // This limits the depth of the frustum to the area we actually care about, and prevents the area we shadow map getting too big.
    //
    //  x                  <-- Camera
    //   \`
    //    \ `
    //     \__`______
    //     |\   `    |     <-- Scene bounds
    //     |_\____`__|
    //        \     `      }
    //         \      `    } This region is redundant
    //          \       `  }

    //
    // TODO: This could be better if we conservatively clip the frustum with the scene's AABB (in all axes) instead,
    // but this works quite well for now with minimal effort. I'm not sure right now how to do that without 
    // building frustum planes and clipping against each individually.
    //

    // Intersect far plane corners againt bottom of the scene.
    Vec3f camPos = Vec3f(math::affineInverse(m_viewMat)[3]);
    Planef lowPlane{ Vec3f(0.f, 1.f, 0.f), -30.f };
    for (int i = 4; i < 8; ++i)
    {
        Vec3f dir = frustumCorners[i] - camPos;
        float t = lowPlane.RayIntersect(Rayf(camPos, dir));
        if (0.f < t && t < 1.f)
        {
            frustumCorners[i] = camPos + t * dir;
        }
    }  

    // Clamp frustum corners against scene bounds
    for (Vec3f& point : frustumCorners)
    {
        point = math::max(point, sceneBounds.m_min);
        point = math::min(point, sceneBounds.m_max);
    }

    // Build an AABB around them
    AABB3f aabb;
    aabb.SetFromPoints((int)std::size(frustumCorners), frustumCorners);
    return aabb;
}

}

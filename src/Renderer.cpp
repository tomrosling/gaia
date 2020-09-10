#include "renderer.hpp"
#include "CommandQueue.hpp"

namespace gaia
{

struct VertexConstantBuffer
{
    Mat4f mvpMatrix;
};

struct PixelConstantBuffer
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

    // Create command allocators
    for (auto& allocator : m_commandAllocators)
    {
        if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
            return false;
    }

    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_copyCommandAllocator))))
        return false;

    // Create command lists
    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_currentBuffer].Get(), nullptr, IID_PPV_ARGS(&m_directCommandList))))
        return false;
    m_directCommandList->Close();

    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, m_copyCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_copyCommandList))))
        return false;
    m_copyCommandList->Close();

    // NOTE: We don't actually create the render and depth targets here, assuming ResizeViewport will be called with an appropriate size.

    m_created = true;
    return true;
}

bool Renderer::ResizeViewport(int width, int height)
{
    assert(width > 0 && height > 0);

    // Tear down existing render targets.
    m_directCommandQueue->Flush();
    m_depthBuffer = nullptr;
    for (auto& rt : m_renderTargets)
    {
        rt = nullptr;
    }

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
                                                 D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(&m_depthBuffer))))
        return false;

    // Create DSV.
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
    dsv.Format = DXGI_FORMAT_D32_FLOAT;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsv.Texture2D.MipSlice = 0;
    dsv.Flags = D3D12_DSV_FLAG_NONE;
    m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsv, m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart());

    m_viewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)width, (float)height);
    m_projMat = math::perspectiveFovRH(0.25f * Pif, m_viewport.Width, m_viewport.Height, 0.01f, 1000.f);
    return true;
}

bool Renderer::LoadCompiledShaders()
{
    // Production: load precompiled shaders
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    if (FAILED(::D3DReadFileToBlob(L"vertex.cso", &vertexShader)))
    {
        DebugOut("Failed to load vertex shader file!");
        return false;
    }

    if (FAILED(::D3DReadFileToBlob(L"pixel.cso", &pixelShader)))
    {
        DebugOut("Failed to load pixel shader file!");
        return false;
    }

    return CreateDefaultPipelineState(vertexShader.Get(), pixelShader.Get());
}

bool Renderer::HotloadShaders()
{
    // Development: compile from files on the fly
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> error;
    if (FAILED(::D3DCompileFromFile(L"vertex.hlsl", nullptr, nullptr, "main", "vs_5_1", 0, 0, &vertexShader, &error)))
    {
        DebugOut("Failed to load vertex shader:\n\n%s\n\n", error->GetBufferPointer());
        return false;
    }

    if (FAILED(::D3DCompileFromFile(L"pixel.hlsl", nullptr, nullptr, "main", "ps_5_1", 0, 0, &pixelShader, &error)))
    {
        DebugOut("Failed to load pixel shader:\n\n%s\n\n", error->GetBufferPointer());
        return false;
    }

    // Force a full CPU/GPU sync then recreate the PSO.
    WaitCurrentFrame();

    return CreateDefaultPipelineState(vertexShader.Get(), pixelShader.Get());
}

bool Renderer::CreateDefaultPipelineState(ID3DBlob* vertexShader, ID3DBlob* pixelShader)
{
    // Define vertex layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOUR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Check root signature 1.1 support...
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    // Create root signature
    CD3DX12_ROOT_PARAMETER1 rootParams[2];
    rootParams[0].InitAsConstants(sizeof(VertexConstantBuffer) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    rootParams[1].InitAsConstants(sizeof(PixelConstantBuffer) / 4, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    rootSignatureDesc.Init_1_1((UINT)std::size(rootParams), rootParams, 0, nullptr, rootSignatureFlags);

    ComPtr<ID3DBlob> rootSigBlob;
    ::D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &rootSigBlob, nullptr);
    if (FAILED(m_device->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
    {
        DebugOut("Failed to create root signature!\n");
        return false;
    }

    // Create PSO
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT inputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY primType;
        CD3DX12_PIPELINE_STATE_STREAM_VS vs;
        CD3DX12_PIPELINE_STATE_STREAM_PS ps;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT dsvFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtvFormats;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC blend;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
    };

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    PipelineStateStream pipelineStateStream;
    pipelineStateStream.rootSignature = m_rootSignature.Get();
    pipelineStateStream.inputLayout = { inputLayout, (UINT)std::size(inputLayout) };
    pipelineStateStream.primType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.vs = CD3DX12_SHADER_BYTECODE(vertexShader);
    pipelineStateStream.ps = CD3DX12_SHADER_BYTECODE(pixelShader);
    pipelineStateStream.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineStateStream.rtvFormats = rtvFormats;
    ((CD3DX12_RASTERIZER_DESC&)pipelineStateStream.rasterizer).FrontCounterClockwise = true;

    // Enable blending.
    // TODO: Separate pass for transparent objects instead.
    D3D12_RENDER_TARGET_BLEND_DESC& rtBlendDesc = ((CD3DX12_BLEND_DESC&)pipelineStateStream.blend).RenderTarget[0];
    rtBlendDesc.BlendEnable = true;
    rtBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rtBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = { sizeof(PipelineStateStream), &pipelineStateStream };
    if (FAILED(m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState))))
    {
        DebugOut("Failed to create pipeline state object!\n");
        return false;
    }

    return true;
}

void Renderer::BeginFrame()
{
    ID3D12CommandAllocator* commandAllocator = m_commandAllocators[m_currentBuffer].Get();
    ID3D12Resource* backBuffer = m_renderTargets[m_currentBuffer].Get();

    // Reset command list
    commandAllocator->Reset();
    m_directCommandList->Reset(commandAllocator, nullptr);

    // Transition to a renderable state
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_directCommandList->ResourceBarrier(1, &barrier);

    // Clear backbuffer
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart(), m_currentBuffer, m_rtvDescriptorSize);
    float clearColor[] = { 0.8f, 0.5f, 0.8f, 0.0f };
    m_directCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    // Clear depth buffer
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(m_dsvDescHeap->GetCPUDescriptorHandleForHeapStart());
    m_directCommandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

    // Set shared state
    m_directCommandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    m_directCommandList->RSSetViewports(1, &m_viewport);
    m_directCommandList->RSSetScissorRects(1, &m_scissorRect);

    // Set PSO/shader state
    m_directCommandList->SetPipelineState(m_pipelineState.Get());
    m_directCommandList->SetGraphicsRootSignature(m_rootSignature.Get());

    // Set global uniforms
    Mat4f viewProjMat = m_projMat * m_viewMat;
    m_directCommandList->SetGraphicsRoot32BitConstants(0, sizeof(Mat4f) / 4, &viewProjMat, offsetof(VertexConstantBuffer, mvpMatrix) / 4);

    Vec3f camPos(math::affineInverse(m_viewMat)[3]);
    m_directCommandList->SetGraphicsRoot32BitConstants(1, sizeof(Vec3f) / 4, &camPos, offsetof(PixelConstantBuffer, camPos) / 4);
}

void Renderer::EndFrame()
{
    // Transition to present state
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_currentBuffer].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_directCommandList->ResourceBarrier(1, &barrier);

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

void Renderer::BeginUploads()
{
    m_copyCommandAllocator->Reset();
    m_copyCommandList->Reset(m_copyCommandAllocator.Get(), nullptr);
}

void Renderer::CreateBuffer(ComPtr<ID3D12Resource>& bufferOut, ComPtr<ID3D12Resource>& intermediateBuffer, size_t size)
{
    // Create destination buffer
    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
    m_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&bufferOut));

    // Create an intermediate buffer to upload via
    // (this must remain in scope in the calling code until the command list has been executed)
    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    m_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&intermediateBuffer));
}

void Renderer::CreateBuffer(ComPtr<ID3D12Resource>& bufferOut, ComPtr<ID3D12Resource>& intermediateBuffer, size_t size, const void* data)
{
    CreateBuffer(bufferOut, intermediateBuffer, size);

    // Upload initial data.
    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = data;
    subresourceData.RowPitch = size;
    subresourceData.SlicePitch = size;
    ::UpdateSubresources(m_copyCommandList.Get(), bufferOut.Get(), intermediateBuffer.Get(), 0, 0, 1, &subresourceData);
}

UINT64 Renderer::EndUploads()
{
    return m_copyCommandQueue->Execute(m_copyCommandList.Get());
}

void Renderer::WaitUploads(UINT64 fenceVal)
{
    m_copyCommandQueue->WaitFence(fenceVal);
}

}

#include "renderer.hpp"
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cassert>
#include "CommandQueue.hpp"

namespace gaia
{

struct Vertex
{
    DirectX::XMFLOAT3 position;
    uint8_t colour[3];
    uint8_t pad;
};

const Vertex VertexData[] = {
    { { -0.5f, -0.5f, 0.f }, { 0xff, 0x00, 0x00 } },
    { {  0.0f,  0.5f, 0.f }, { 0x00, 0xff, 0x00 } },
    { {  0.5f, -0.5f, 0.f }, { 0x00, 0x00, 0xff } }
};

const uint16_t IndexData[] = { 0, 1, 2 };

// TODO! This currently seems to be resampled to fit window size
const uint32_t Width = 800;
const uint32_t Height = 600;

Renderer::Renderer()
    : m_viewport(CD3DX12_VIEWPORT(0.f, 0.f, (float)Width, (float)Height))
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
    swapchainDesc.Width = Width;
    swapchainDesc.Height = Height;
    swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapchainDesc.SampleDesc.Count = 1;
    if (!SUCCEEDED(m_factory->CreateSwapChainForHwnd(m_directCommandQueue->GetCommandQueue(), hwnd, &swapchainDesc, nullptr, nullptr, &swapChain1)))
        return false;

    // Cast to IDXGISwapChain3
    if (!SUCCEEDED(swapChain1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_swapChain)))
        return false;

    m_currentBuffer = m_swapChain->GetCurrentBackBufferIndex();

    // Create RTV descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = BackbufferCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvDescHeap))))
        return false;

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create an RTV for each frame
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < BackbufferCount; ++i)
    {
        if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))))
            return false;

        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(m_rtvDescriptorSize);
    }

    // Create command allocators
    for (int i = 0; i < BackbufferCount; ++i)
    {
        if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]))))
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

    // Leave copy command list open, so we can immediately use it to upload data.

    m_created = true;
    return true;
}

bool Renderer::CreateHelloTriangle()
{
    // Upload vertex buffer (must be done via an intermediate resource)
    ComPtr<ID3D12Resource> intermediateVB;
    CreateBuffer(m_vertexBuffer, intermediateVB, sizeof(VertexData), VertexData);

    // Create vertex buffer view
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = sizeof(VertexData);
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);

    // Upload index buffer
    ComPtr<ID3D12Resource> intermediateIB;
    CreateBuffer(m_indexBuffer, intermediateIB, sizeof(IndexData), IndexData);
    
    // Create index buffer view
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
    m_indexBufferView.SizeInBytes = sizeof(IndexData);

    // Load shaders
    // TODO: Add shader hotload support (D3DCompileFromFile)
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    if (!SUCCEEDED(::D3DReadFileToBlob(L"vertex.cso", &vertexShader)))
    {
        printf("Failed to load vertex shader!\n");
        return false;
    }

    if(!SUCCEEDED(::D3DReadFileToBlob(L"pixel.cso", &pixelShader)))
    {
        printf("Failed to load pixel shader!\n");
        return false;
    }

    // Define vertex layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
    rootSignatureDesc.Init_1_1(0, nullptr, 0, nullptr, rootSignatureFlags);

    ComPtr<ID3DBlob> rootSigBlob;
    ::D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &rootSigBlob, nullptr);
    if (FAILED(m_device->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
    {
        printf("Failed to create root signature!\n");
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
    };
    
    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    PipelineStateStream pipelineStateStream;
    pipelineStateStream.rootSignature = m_rootSignature.Get();
    pipelineStateStream.inputLayout = { inputLayout, (UINT)std::size(inputLayout) };
    pipelineStateStream.primType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.vs = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    pipelineStateStream.ps = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    pipelineStateStream.rtvFormats = rtvFormats;

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = { sizeof(PipelineStateStream), &pipelineStateStream };
    if (FAILED(m_device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState))))
    {
        printf("Failed to create pipeline state object!\n");
        return false;
    }

    UINT64 fenceVal = m_copyCommandQueue->Execute(m_copyCommandList.Get());
    m_copyCommandQueue->WaitFence(fenceVal);
    return true;
}

void Renderer::Render()
{
    ID3D12CommandAllocator* commandAllocator = m_commandAllocators[m_currentBuffer].Get();
    ID3D12Resource* backBuffer = m_renderTargets[m_currentBuffer].Get();
    BeginFrame(commandAllocator, backBuffer);

    // Draw our lovely triangle    
    m_directCommandList->SetPipelineState(m_pipelineState.Get());
    m_directCommandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_directCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_directCommandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_directCommandList->IASetIndexBuffer(&m_indexBufferView);
    m_directCommandList->DrawIndexedInstanced((UINT)std::size(IndexData), 1, 0, 0, 0);

    EndFrame(backBuffer);
}

void Renderer::BeginFrame(ID3D12CommandAllocator* commandAllocator, ID3D12Resource* backBuffer)
{
    // Reset command list
    commandAllocator->Reset();
    m_directCommandList->Reset(commandAllocator, nullptr);

    // Transition to a renderable state
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_directCommandList->ResourceBarrier(1, &barrier);

    // Clear backbuffer
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart(), m_currentBuffer, m_rtvDescriptorSize);
    float clearColor[] = { 0.8f, 0.5f, 0.8f, 1.0f };
    m_directCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    // Set shared state
    m_directCommandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    m_directCommandList->RSSetViewports(1, &m_viewport);
    m_directCommandList->RSSetScissorRects(1, &m_scissorRect);
}

void Renderer::EndFrame(ID3D12Resource* backBuffer)
{
    // Transition to present state
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_directCommandList->ResourceBarrier(1, &barrier);

    // Submit draw (etc) commands
    m_frameFenceValues[m_currentBuffer] = m_directCommandQueue->Execute(m_directCommandList.Get());

    // Present buffer
    m_swapChain->Present(1, 0);
    m_currentBuffer = m_swapChain->GetCurrentBackBufferIndex();

    // Wait for previous frame's fence
    m_directCommandQueue->WaitFence(m_frameFenceValues[m_currentBuffer]);
}

void Renderer::CreateBuffer(ComPtr<ID3D12Resource>& bufferOut, ComPtr<ID3D12Resource>& intermediateBuffer, size_t size, const void* data)
{
    // Create destination buffer
    m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_NONE),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&bufferOut));

    // Create an intermediate buffer to upload via
    // (this must remain in scope in the calling code until the command list has been executed)
    m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(size),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&intermediateBuffer));

    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = data;
    subresourceData.RowPitch = size;
    subresourceData.SlicePitch = size;
    UpdateSubresources(m_copyCommandList.Get(), bufferOut.Get(), intermediateBuffer.Get(), 0, 0, 1, &subresourceData);
}

}

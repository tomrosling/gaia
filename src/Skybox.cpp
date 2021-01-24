#include "Skybox.hpp"
#include "Renderer.hpp"

namespace gaia
{

bool Skybox::Init(Renderer& renderer)
{
    // Production: load precompiled shaders
    ComPtr<ID3DBlob> vertexShader = renderer.LoadCompiledShader(L"SkyboxVertex.cso");
    ComPtr<ID3DBlob> pixelShader = renderer.LoadCompiledShader(L"SkyboxPixel.cso");
    if (!(vertexShader && pixelShader))
        return false;

    if (!CreatePipelineState(renderer, vertexShader.Get(), pixelShader.Get()))
        return false;

    renderer.BeginUploads();
    m_cubemapSrvIndex = renderer.LoadTexture(m_cubemapTexResource, m_intermediateCubemapTexResource, L"skymap.dds");

    using SkyboxVertex = Vec3f;
    const SkyboxVertex VertexData[] = {
        { -1.f, -1.f, -1.f },
        {  1.f, -1.f, -1.f },
        { -1.f,  1.f, -1.f },
        {  1.f,  1.f, -1.f },
        { -1.f, -1.f,  1.f },
        {  1.f, -1.f,  1.f },
        { -1.f,  1.f,  1.f },
        {  1.f,  1.f,  1.f },
    };

    const uint16 IndexData[] = {
        0, 2, 1,   1, 2, 3,
        0, 4, 2,   2, 4, 6,
        4, 5, 6,   6, 5, 7,
        5, 1, 7,   7, 1, 3,
        0, 1, 4,   4, 1, 5,
        3, 2, 6,   6, 7, 3
    };

    renderer.CreateBuffer(m_vertexBuffer, m_intermediateVertexBuffer, sizeof(VertexData), VertexData);
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = sizeof(VertexData);
    m_vertexBufferView.StrideInBytes = sizeof(SkyboxVertex);

    renderer.CreateBuffer(m_indexBuffer, m_intermediateIndexBuffer, sizeof(IndexData), IndexData);
    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
    m_indexBufferView.SizeInBytes = sizeof(IndexData);

    m_uploadFenceVal = renderer.EndUploads();

    return true;
}

void Skybox::Render(Renderer& renderer)
{
    if (m_uploadFenceVal != 0)
    {
        renderer.WaitUploads(m_uploadFenceVal);
        m_uploadFenceVal = 0;
    }

    ID3D12GraphicsCommandList& commandList = renderer.GetDirectCommandList();
    commandList.SetPipelineState(m_pipelineState.Get());
    renderer.BindDescriptor(m_cubemapSrvIndex, RootParam::Texture0);

    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList.IASetVertexBuffers(0, 1, &m_vertexBufferView);
    commandList.IASetIndexBuffer(&m_indexBufferView);
    commandList.DrawIndexedInstanced(m_indexBufferView.SizeInBytes / sizeof(uint16), 1, 0, 0, 0);
}

bool Skybox::CreatePipelineState(Renderer& renderer, ID3DBlob* vertexShader, ID3DBlob* pixelShader)
{
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT inputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY primType;
        CD3DX12_PIPELINE_STATE_STREAM_VS vs;
        CD3DX12_PIPELINE_STATE_STREAM_PS ps;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT dsvFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtvFormats;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depthStencil;
    };

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    PipelineStateStream pipelineStateStream;
    pipelineStateStream.rootSignature = &renderer.GetRootSignature();
    pipelineStateStream.inputLayout = { inputLayout, (UINT)std::size(inputLayout) };
    pipelineStateStream.primType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineStateStream.vs = CD3DX12_SHADER_BYTECODE(vertexShader);
    pipelineStateStream.ps = CD3DX12_SHADER_BYTECODE(pixelShader);
    pipelineStateStream.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineStateStream.rtvFormats = rtvFormats;
    ((CD3DX12_DEPTH_STENCIL_DESC&)pipelineStateStream.depthStencil).DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = { sizeof(PipelineStateStream), &pipelineStateStream };
    if (FAILED(renderer.GetDevice().CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState))))
    {
        DebugOut("Failed to create pipeline state object!\n");
        return false;
    }

    return true;
}


}

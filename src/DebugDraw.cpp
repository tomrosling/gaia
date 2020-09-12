#include "DebugDraw.hpp"
#include "Renderer.hpp"

namespace gaia
{

struct DebugDraw::DebugVertex
{
    Vec3f position;
    Vec4u8 colour;
};

static constexpr int BufferSize = 1024 * 1024;
static constexpr int MaxVertices = BufferSize / sizeof(DebugDraw::DebugVertex);

void DebugDraw::Init(Renderer& renderer)
{
    // Create a PSO
    ComPtr<ID3DBlob> vertexShader;
    HRESULT result = ::D3DReadFileToBlob(L"DebugVertex.cso", &vertexShader);
    Assert(SUCCEEDED(result));

    ComPtr<ID3DBlob> pixelShader;
    result = ::D3DReadFileToBlob(L"DebugPixel.cso", &pixelShader);
    Assert(SUCCEEDED(result));

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

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOUR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    PipelineStateStream pipelineStateStream;
    pipelineStateStream.rootSignature = &renderer.GetRootSignature();
    pipelineStateStream.inputLayout = { inputLayout, (UINT)std::size(inputLayout) };
    pipelineStateStream.primType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    pipelineStateStream.vs = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    pipelineStateStream.ps = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    pipelineStateStream.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineStateStream.rtvFormats = rtvFormats;

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = { sizeof(PipelineStateStream), &pipelineStateStream };
    result = renderer.GetDevice().CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState));
    Assert(SUCCEEDED(result));

    // Create buffers
    for (ComPtr<ID3D12Resource>& buf : m_doubleVertexBuffer)
    {
        buf = renderer.CreateResidentBuffer(BufferSize);
    }

    m_uploadBuffer = renderer.CreateUploadBuffer(BufferSize);

    D3D12_RANGE readRange = { 1, 0 };
    m_uploadBuffer->Map(0, &readRange, (void**)&m_mappedVertexBuffer);
    Assert(m_mappedVertexBuffer);
}

void DebugDraw::Render(Renderer& renderer)
{
    if (m_usedVertices == 0)
        return;

    // Upload and stall :(
    renderer.BeginUploads();
    renderer.GetCopyCommandList().CopyBufferRegion(m_doubleVertexBuffer[m_currentBuffer].Get(), 0,
        m_uploadBuffer.Get(), 0, m_usedVertices * sizeof(DebugVertex));
    renderer.WaitUploads(renderer.EndUploads());

    D3D12_VERTEX_BUFFER_VIEW view = {
        m_doubleVertexBuffer[m_currentBuffer]->GetGPUVirtualAddress(),
        (UINT)m_usedVertices * sizeof(DebugVertex),
        sizeof(DebugVertex)
    };

    ID3D12GraphicsCommandList& commandList = renderer.GetDirectCommandList();
    commandList.SetPipelineState(m_pipelineState.Get());
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    commandList.IASetVertexBuffers(0, 1, &view);
    commandList.DrawInstanced(view.SizeInBytes / view.StrideInBytes, 1, 0, 0);

    m_currentBuffer ^= 1;
    m_usedVertices = 0;
}

void DebugDraw::Point(Vec3f pos, float halfSize, Vec4u8 col)
{
    Vec3f points[] = {
        pos + Vec3f(-halfSize, 0.f, 0.f),
        pos + Vec3f( halfSize, 0.f, 0.f),
        pos + Vec3f(0.f, -halfSize, 0.f),
        pos + Vec3f(0.f,  halfSize, 0.f),
        pos + Vec3f(0.f, 0.f, -halfSize),
        pos + Vec3f(0.f, 0.f,  halfSize),
    };
    
    Lines((int)std::size(points), points, col);
}

void DebugDraw::Lines(int numPoints, const Vec3f* points, Vec4u8 col)
{
    Assert(numPoints > 1);
    Assert(numPoints % 2 == 0);
    Assert(m_usedVertices + numPoints <= MaxVertices);
    Assert(m_uploadBuffer);

    int vertex = m_usedVertices;
    for (int i = 0; i < numPoints; ++i)
    {
        DebugVertex& v0 = m_mappedVertexBuffer[vertex++];
        v0.position = points[i];
        v0.colour = col;
    }
    m_usedVertices = vertex;
}

}

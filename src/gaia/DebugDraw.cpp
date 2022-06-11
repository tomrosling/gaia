#include "DebugDraw.hpp"
#include "Renderer.hpp"
#include "Math/AABB.hpp"

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
    ComPtr<ID3DBlob> vertexShader = renderer.LoadCompiledShader(L"DebugVertex.cso");
    Assert(vertexShader);

    ComPtr<ID3DBlob> pixelShader = renderer.LoadCompiledShader(L"DebugPixel.cso");
    Assert(pixelShader);

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
    ((D3D12_DEPTH_STENCIL_DESC&)pipelineStateStream.depthStencil).DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = { sizeof(PipelineStateStream), &pipelineStateStream };
    HRESULT result = renderer.GetDevice().CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState));
    Assert(SUCCEEDED(result));

    // Create buffers
    for (ComPtr<ID3D12Resource>& buf : m_doubleVertexBuffer)
    {
        buf = renderer.CreateResidentBuffer(BufferSize);
    }

    m_uploadBuffer = renderer.CreateUploadBuffer(BufferSize);

    D3D12_RANGE readRange = {};
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

void DebugDraw::DrawPoint(Vec3f pos, float halfSize, Vec4u8 col)
{
    Vec3f points[] = {
        pos + Vec3f(-halfSize, 0.f, 0.f),
        pos + Vec3f( halfSize, 0.f, 0.f),
        pos + Vec3f(0.f, -halfSize, 0.f),
        pos + Vec3f(0.f,  halfSize, 0.f),
        pos + Vec3f(0.f, 0.f, -halfSize),
        pos + Vec3f(0.f, 0.f,  halfSize),
    };
    
    DrawLines((int)std::size(points), points, col);
}

void DebugDraw::DrawLines(int numPoints, const Vec3f* points, Vec4u8 col)
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

void DebugDraw::DrawTransform(const Mat4f& xform, float size)
{
    Vec3f centre(xform[3]);
    Vec3f right = math::Mat4fTransformVec3f(xform, Vec3fX * size);
    Vec3f up = math::Mat4fTransformVec3f(xform, Vec3fY * size);
    Vec3f back = math::Mat4fTransformVec3f(xform, Vec3fZ * size);

    Vec3f points[] = {
        centre, right,
        centre, up,
        centre, back
    };

    DrawLines(2, points + 0, Vec4u8(0xff, 0x00, 0x00, 0xff));
    DrawLines(2, points + 2, Vec4u8(0x00, 0xff, 0x00, 0xff));
    DrawLines(2, points + 4, Vec4u8(0x00, 0x00, 0xff, 0xff));
}

void DebugDraw::DrawAABB3f(const AABB3f& aabb, Vec4u8 col, const Mat4f& xform)
{
    Vec3f a = aabb.m_min;
    Vec3f b(aabb.m_max.x, aabb.m_min.y, aabb.m_min.z);
    Vec3f c(aabb.m_min.x, aabb.m_max.y, aabb.m_min.z);
    Vec3f d(aabb.m_max.x, aabb.m_max.y, aabb.m_min.z);
    Vec3f e(aabb.m_min.x, aabb.m_min.y, aabb.m_max.z);
    Vec3f f(aabb.m_max.x, aabb.m_min.y, aabb.m_max.z);
    Vec3f g(aabb.m_min.x, aabb.m_max.y, aabb.m_max.z);
    Vec3f h = aabb.m_max;

    Vec3f points[] = {
        a, b, a, c, b, d, c, d,
        e, f, e, g, f, h, g, h,
        a, e, b, f, c, g, d, h
    };

    if (xform != Mat4fIdentity)
    {
        for (Vec3f& p : points)
        {
            p = math::Mat4fTransformVec3f(xform, p);
        }
    }

    DrawLines((int)std::size(points), points, col);
}

}

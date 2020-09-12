#pragma once

namespace gaia
{

class Renderer;

class DebugDraw
{
public:
    struct DebugVertex;

    static DebugDraw& Instance()
    {
        static DebugDraw inst;
        return inst;
    }

    void Init(Renderer& renderer);
    void Render(Renderer& renderer);
    void Point(Vec3f pos, float halfSize, Vec4u8 col);
    void Lines(int numPoints, const Vec3f* points, Vec4u8 col);

private:
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12Resource> m_doubleVertexBuffer[2];
    ComPtr<ID3D12Resource> m_uploadBuffer;
    DebugVertex* m_mappedVertexBuffer = nullptr;
    int m_currentBuffer = 0;
    int m_usedVertices = 0;
};

}

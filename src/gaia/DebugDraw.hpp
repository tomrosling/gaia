#pragma once

namespace gaia
{

class AABB3f;
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
    void DrawPoint(Vec3f pos, float halfSize, Vec4u8 col);
    void DrawLines(int numPoints, const Vec3f* points, Vec4u8 col);
    void DrawTransform(const Mat4f& xform, float size = 1.f);
    void DrawAABB3f(const AABB3f& aabb, Vec4u8 col, const Mat4f& xform = Mat4fIdentity);

private:
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12Resource> m_doubleVertexBuffer[2];
    ComPtr<ID3D12Resource> m_uploadBuffer;
    DebugVertex* m_mappedVertexBuffer = nullptr;
    int m_currentBuffer = 0;
    int m_usedVertices = 0;
};

}

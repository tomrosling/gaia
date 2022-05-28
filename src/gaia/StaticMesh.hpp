#pragma once

namespace gaia
{

class Renderer;

/*
 * A basic, no-frills static mesh. Pass in vertex data of whatever format your shader requires.
 */
class StaticMesh
{
public:
    void Init(Renderer& renderer, const Span<const uchar>& vertexData, int vertexStride, const Span<const uint16>& indexData);
    void Render(Renderer& renderer);

private:
    VertexBuffer m_vb;
    IndexBuffer m_ib;
};

}

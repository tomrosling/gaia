#pragma once

namespace gaia
{

class Renderer;
struct Vertex;

class Terrain
{
public:
    struct VertexBuffer
    {
        ComPtr<ID3D12Resource> buffer;
        ComPtr<ID3D12Resource> intermediateBuffer;
        D3D12_VERTEX_BUFFER_VIEW view = {};
    };

    struct IndexBuffer
    {
        ComPtr<ID3D12Resource> buffer;
        ComPtr<ID3D12Resource> intermediateBuffer;
        D3D12_INDEX_BUFFER_VIEW view = {};
    };

    void Build(Renderer& renderer);
    void Render(Renderer& renderer);
    void RaiseAreaRounded(Renderer& renderer, Vec2f posXZ, float radius, float raiseBy);

private:
    void BuildIndexBuffer(Renderer& renderer);
    void BuildTile(Renderer& renderer, int tileX, int tileZ);
    void BuildWater(Renderer& renderer);
    Vec3f GeneratePos(int globalX, int globalZ);
    Vec4u8 GenerateCol(float height);
    Vec3f GenerateNormal(const Vertex* vertexData, Vec2i vertexCoords, Vec2i tileCoords);

    std::vector<VertexBuffer> m_tileVertexBuffers;
    IndexBuffer m_indexBuffer; // Indices are the same for each tile.
    int m_seed = 0;

    VertexBuffer m_waterVertexBuffer;
    IndexBuffer m_waterIndexBuffer;
};

}

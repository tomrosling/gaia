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
        ComPtr<ID3D12Resource> gpuDoubleBuffer[2];
        D3D12_VERTEX_BUFFER_VIEW views[2] = {};
        ComPtr<ID3D12Resource> intermediateBuffer;
        std::vector<float> heightmap;
        int currentBuffer = 0;
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
    void UpdateVertex(Vertex* mappedVertexData, const std::vector<float>& heightmap, Vec2i vertexCoords, Vec2i tileCoords);
    float GenerateHeight(int globalX, int globalZ);
    Vec3f ToVertexPos(int globalX, float height, int globalZ);
    Vec4u8 GenerateCol(float height);
    Vec3f GenerateNormal(const std::vector<float>& heightmap, Vec2i vertexCoords, Vec2i tileCoords);

    std::vector<VertexBuffer> m_tileVertexBuffers;
    IndexBuffer m_indexBuffer; // Indices are the same for each tile.
    uint64_t m_uploadFenceVal = 0;
    int m_seed = 0;

    ComPtr<ID3D12Resource> m_waterVertexBuffer;
    ComPtr<ID3D12Resource> m_waterIntermediateVertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_waterVertexBufferView;
    IndexBuffer m_waterIndexBuffer;
};

}

#pragma once

namespace gaia
{

class Renderer;
struct Vertex;

class Terrain
{
public:
    struct Tile
    {
        ComPtr<ID3D12Resource> gpuDoubleBuffer[2];
        D3D12_VERTEX_BUFFER_VIEW views[2] = {};
        ComPtr<ID3D12Resource> intermediateBuffer;
        std::vector<float> heightmap;
        Vec2i dirtyMin = Vec2iZero;
        Vec2i dirtyMax = Vec2iZero;
        int currentBuffer = 0;
    };

    struct IndexBuffer
    {
        ComPtr<ID3D12Resource> buffer;
        ComPtr<ID3D12Resource> intermediateBuffer;
        D3D12_INDEX_BUFFER_VIEW view = {};
    };

    struct TerrainPSConstantBuffer
    {
        Vec2f hightlightPosXZ;
        float highlightRadiusSq;
    };

    void Build(Renderer& renderer);
    void Render(Renderer& renderer);
    void RaiseAreaRounded(Renderer& renderer, Vec2f posXZ, float radius, float raiseBy);

    bool LoadCompiledShaders(Renderer& renderer);
    bool HotloadShaders(Renderer& renderer);

    void SetHighlightPos(Vec2f posXZ, int currentBuffer) { m_mappedConstantBuffers[currentBuffer]->hightlightPosXZ = posXZ; }
    void SetHighlightRadius(float radius, int currentBuffer) { m_mappedConstantBuffers[currentBuffer]->highlightRadiusSq = math::Square(radius); }

private:
    bool CreatePipelineState(Renderer& renderer, ID3DBlob* vertexShader, ID3DBlob* pixelShader);
    void CreateConstantBuffers(Renderer& renderer);
    void BuildIndexBuffer(Renderer& renderer);
    void BuildTile(Renderer& renderer, int tileX, int tileZ);
    void BuildWater(Renderer& renderer);
    void UpdateVertex(Vertex* mappedVertexData, const std::vector<float>& heightmap, Vec2i vertexCoords, Vec2i tileCoords);
    float GenerateHeight(int globalX, int globalZ);
    Vec3f ToVertexPos(int globalX, float height, int globalZ);
    Vec4u8 GenerateCol(float height);
    Vec3f GenerateNormal(const std::vector<float>& heightmap, Vec2i vertexCoords, Vec2i tileCoords);

    std::vector<Tile> m_tiles;
    IndexBuffer m_indexBuffer; // Indices are the same for each tile.
    uint64 m_uploadFenceVal = 0;
    int m_seed = 0;

    ComPtr<ID3D12Resource> m_waterVertexBuffer;
    ComPtr<ID3D12Resource> m_waterIntermediateVertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_waterVertexBufferView;
    IndexBuffer m_waterIndexBuffer;

    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12Resource> m_constantBuffers[BackbufferCount];
    TerrainPSConstantBuffer* m_mappedConstantBuffers[BackbufferCount] = {};
    int m_cbufferDescIndex = 0;
};

}

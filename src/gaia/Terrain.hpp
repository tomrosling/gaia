#pragma once

namespace gaia
{

class Renderer;
class TerrainComputeNormals;
struct TerrainVertex;
struct WaterVertex;

class Terrain
{
public:
    Terrain();
    ~Terrain();

    bool Init(Renderer& renderer);
    void Build(Renderer& renderer);
    void Render(Renderer& renderer);
    void RaiseAreaRounded(Renderer& renderer, Vec2f posXZ, float radius, float raiseBy);

    bool LoadCompiledShaders(Renderer& renderer);
    bool HotloadShaders(Renderer& renderer);

    void SetHighlightPos(Vec2f posXZ, int currentBuffer) { m_mappedConstantBuffers[currentBuffer]->highlightPosXZ = posXZ; }
    void SetHighlightRadius(float radius, int currentBuffer) { m_mappedConstantBuffers[currentBuffer]->highlightRadiusSq = math::Square(radius); }

    void Imgui(Renderer& renderer);

private:
    using HeightmapData = std::vector<float>;
    static constexpr int NumClipLevels = 8; // Number of clipmap levels (i.e. number of textures).

    struct ClipmapLevel
    {
        ComPtr<ID3D12Resource> heightMap;
        ComPtr<ID3D12Resource> normalMap;
        ComPtr<ID3D12Resource> intermediateBuffer; // TODO: Optimise this. We don't need a separate intermediate buffer per layer.
    };

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

    struct TerrainPSConstantBuffer
    {
        Vec2f highlightPosXZ;
        Vec2f clipmapUVOffset;
        float highlightRadiusSq;
    };

    struct NoiseOctave
    {
        float frequency;
        float amplitude;
    };

    bool CreatePipelineState(Renderer& renderer, ID3DBlob* vertexShader, ID3DBlob* hullShader, ID3DBlob* domainShader, ID3DBlob* pixelShader);
    bool CreateWaterPipelineState(Renderer& renderer, ID3DBlob* vertexShader, ID3DBlob* pixelShader);
    void CreateConstantBuffers(Renderer& renderer);
    void BuildIndexBuffer(Renderer& renderer);
    void BuildVertexBuffer(Renderer& renderer);
    void BuildWater(Renderer& renderer);
    void UpdateClipmapTextures(Renderer& renderer);
    void UpdateClipmapTextureLevel(Renderer& renderer, int level, Vec2i oldTexelOffset, Vec2i newTexelOffset);
    void UploadClipmapTextureRegion(Renderer& renderer, int level, Vec2i globalMin, Vec2i globalMax, Vec2i newTexelOffset);
    HeightmapData& GetOrCreateTile(Vec2i tile, int level);
    float GetHeight(Vec2i levelGlobalCoords, int level) const;
    float GenerateHeight(Vec2i levelGlobalCoords, int level) const;
    Vec2f ToVertexPos(int globalX, int globalZ);
    Vec2i CalcClipmapTexelOffset(const Vec3f& camPos) const;
    void WriteIntermediateTextureData(float* mappedHeights, int level, Vec2i levelGlobalMin, Vec2i levelGlobalMax);

    // Rendering objects.
    ComPtr<ID3D12PipelineState> m_pipelineState;
    std::unique_ptr<TerrainComputeNormals> m_computeNormals;

    // Heightmap data, lazily populated as tiles are edited (otherwise data is just created from noise on demand).
    std::unordered_map<Vec2i, HeightmapData> m_tileCaches[NumClipLevels];

    // Clipmap and vertex data.
    ClipmapLevel m_clipmapLevels[NumClipLevels];
    VertexBuffer m_vertexBuffer;
    IndexBuffer m_indexBuffer;
    uint64 m_uploadFenceVal = 0;
    uint64 m_computeFenceVal = 0;
    Vec2i m_clipmapTexelOffset = Vec2iZero;
    Vec2i m_globalDirtyRegionMin = Vec2iZero;
    Vec2i m_globalDirtyRegionMax = Vec2iZero; // Inclusive bounds.
    
    // Water rendering data (TODO: Move water to it's own class).
    VertexBuffer m_waterVertexBuffer;
    IndexBuffer m_waterIndexBuffer;
    ComPtr<ID3D12PipelineState> m_waterPipelineState;

    // Constants, textures.
    static const int NumDetailTextureSets = 2;
    ComPtr<ID3D12Resource> m_constantBuffers[BackbufferCount];
    TerrainPSConstantBuffer* m_mappedConstantBuffers[BackbufferCount] = {};
    int m_cbufferDescIndex = -1;
    int m_diffuseTexDescIndices[2] = { -1, -1 };
    int m_normalTexDescIndices[2] = { -1, -1 };
    int m_baseHeightMapTexIndex = -1;
    int m_baseNormalMapTexIndex = -1;
    int m_heightmapSamplerDescIndex = -1;
    bool m_detailTexStateDirty = true;
    ComPtr<ID3D12Resource> m_diffuseTextures[NumDetailTextureSets];
    ComPtr<ID3D12Resource> m_intermediateDiffuseTexBuffers[NumDetailTextureSets];
    ComPtr<ID3D12Resource> m_detailNormalMaps[NumDetailTextureSets];
    ComPtr<ID3D12Resource> m_intermediateNoramlMapBuffers[NumDetailTextureSets];

    // Tweakables/generation data.
    int m_seed = 0;
    float m_baseHeight = 0.f;
    NoiseOctave m_ridgeNoiseParams[2] = {};
    NoiseOctave m_ridgeNoiseMultiplierParams[1] = {};
    NoiseOctave m_whiteNoiseParams[4] = {};
    bool m_randomiseSeed = true;
    bool m_wireframeMode = false;
    bool m_freezeClipmap = false;
};

}

#include "Terrain.hpp"
#include "TerrainComputeNormals.hpp"
#include "TerrainConstants.hpp"
#include "Renderer.hpp"
#include <DirectXTex/DirectXTex.h>
#include <stb_perlin.h>

namespace gaia
{

using namespace TerrainConstants;

struct TerrainVertex
{
    Vec2f pos;
};

struct WaterVertex
{
    Vec3f position;
    Vec3f normal;
    Vec4u8 colour;
};


// Returns index of a heightmap sample within a tile.
static int TileIndex(int x, int z)
{
    Assert(0 <= x && x < TileDimension);
    Assert(0 <= z && z < TileDimension);
    return TileDimension * z + x;
}

static int TileIndex(Vec2i coords)
{
    return TileIndex(coords.x, coords.y);
}

// Returns index of a vertex within the vertex buffer.
static int VertexIndex(int x, int z) 
{
    Assert(0 <= x && x < VertexGridDimension);
    Assert(0 <= z && z < VertexGridDimension);
    return VertexGridDimension * z + x;
}

static int VertexIndex(Vec2i coords)
{
    return VertexIndex(coords.x, coords.y);
}

// Returns index of a sample within a heightmap/clipmap texture.
static int HeightmapIndex(int x, int z)
{
    Assert(0 <= x && x < HeightmapDimension);
    Assert(0 <= z && z < HeightmapDimension);
    return HeightmapDimension * z + x;
}

static int HeightmapIndex(Vec2i coords)
{
    return HeightmapIndex(coords.x, coords.y);
}

static Vec2i WrapHeightmapCoords(Vec2i levelGlobalCoords)
{
    // Turns a "world" UV coordinate into a local one.
    // This is a mod operation that always returns a positive result.
    static_assert(math::IsPow2(HeightmapDimension), "HeightmapDimension must be a power of 2");
    return levelGlobalCoords & (HeightmapDimension - 1);
}

static Vec2i WrapTileCoords(Vec2i levelGlobalCoords)
{
    static_assert(math::IsPow2(TileDimension), "TileDimension must be a power of 2");
    return levelGlobalCoords & (TileDimension - 1);
}

static std::pair<Vec2i, Vec2i> LevelGlobalCoordsToTile(Vec2i levelGlobalCoords)
{
    Vec2i coordsInTile = WrapTileCoords(levelGlobalCoords);
    Vec2i tile = (levelGlobalCoords - coordsInTile) / TileDimension;
    return { tile, coordsInTile };
}

static std::pair<Vec2i, Vec2i> GlobalCoordsToTile(Vec2i globalCoords, int level)
{
    return LevelGlobalCoordsToTile(Vec2i(globalCoords) >> level);
}

// Returns coordinates relative to the given tile; may be out of bounds for that tile.
static Vec2i LevelGlobalCoordsToTileCoords(Vec2i levelGlobalCoords, Vec2i tile)
{
    return levelGlobalCoords - (TileDimension * tile);
}

// Returns coordinates relative to the given tile; may be out of bounds for that tile.
static Vec2i GlobalCoordsToTileCoords(Vec2i globalCoords, Vec2i tile, int level)
{
    return LevelGlobalCoordsToTileCoords(Vec2i(globalCoords) >> level, tile);
}

static Vec2i WorldPosToGlobalCoords(Vec2f worldPos)
{
    return math::Vec2Floor(worldPos / TexelSize);
}

static std::pair<Vec2i, Vec2i> WorldPosToTile(Vec2f worldPos, int level)
{
    return GlobalCoordsToTile(WorldPosToGlobalCoords(worldPos), level);
}

static Vec2f GlobalCoordsToWorldPos(Vec2i globalCoords)
{
    return Vec2f(globalCoords) * TexelSize;
}

static D3D12_TEXTURE_COPY_LOCATION MakeSrcTexCopyLocation(ID3D12Resource* intermediateBuffer, DXGI_FORMAT format)
{
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = intermediateBuffer;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = 0;
    src.PlacedFootprint.Footprint.Format = format;
    src.PlacedFootprint.Footprint.Width = HeightmapDimension;
    src.PlacedFootprint.Footprint.Height = HeightmapDimension;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = GetTexturePitchBytes(HeightmapDimension, DirectX::BitsPerPixel(format) / 8);
    return src;
}

static D3D12_TEXTURE_COPY_LOCATION MakeDstTexCopyLocation(ID3D12Resource* texture)
{

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    return dst;
}

static void CopyTex2DRegion(ID3D12GraphicsCommandList& commandList, D3D12_TEXTURE_COPY_LOCATION& dst, const D3D12_TEXTURE_COPY_LOCATION& src, Vec2i minInclusive, Vec2i maxExclusive)
{
    D3D12_BOX box;
    box.left = minInclusive.x;
    box.top = minInclusive.y;
    box.front = 0;
    box.right = maxExclusive.x;
    box.bottom = maxExclusive.y;
    box.back = 1;
    commandList.CopyTextureRegion(&dst, box.left, box.top, 0, &src, &box);
}


Terrain::Terrain()
    : m_baseHeight(-12.f)
    , m_ridgeNoiseParams{ { 0.001f, 16.f },
                          { 0.002f, 6.f } }
    , m_ridgeNoiseMultiplierParams{ { 0.001f, 0.25f } }
    , m_whiteNoiseParams{ { 0.005f, 3.5f },
                          { 0.01f, 1.0f },
                          { 0.02f, 0.5f },
                          { 0.1f, 0.03f } }
{
}

Terrain::~Terrain() = default;

bool Terrain::Init(Renderer& renderer)
{
    m_computeNormals = std::make_unique<TerrainComputeNormals>();
    if (!m_computeNormals->Init(renderer))
        return false;

    CreateConstantBuffers(renderer);

    renderer.BeginUploads();

    m_diffuseTexDescIndices[0] = renderer.LoadTexture(m_diffuseTextures[0], m_intermediateDiffuseTexBuffers[0], L"aerial_grass_rock_diff_1k.png");
    m_diffuseTexDescIndices[1] = renderer.LoadTexture(m_diffuseTextures[1], m_intermediateDiffuseTexBuffers[1], L"ground_grey_diff_1k.png");
    m_normalTexDescIndices[0] = renderer.LoadTexture(m_detailNormalMaps[0], m_intermediateNoramlMapBuffers[0], L"aerial_grass_rock_nor_dx_1k.png");
    m_normalTexDescIndices[1] = renderer.LoadTexture(m_detailNormalMaps[1], m_intermediateNoramlMapBuffers[1], L"ground_grey_nor_dx_1k.png");

    // Create a set of clipmap textures.
    ID3D12Resource* heightMaps[NumClipLevels] = {};
    ID3D12Resource* normalMaps[NumClipLevels] = {};
    for (int i = 0; i < NumClipLevels; ++i)
    {
        Renderer::Texture2DParams texParams;
        texParams.width = HeightmapDimension;
        texParams.height = HeightmapDimension;

        ClipmapLevel& tile = m_clipmapLevels[i];
        texParams.format = HeightmapTexFormat;
        texParams.initialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; // We'll transition this to and from D3D12_RESOURCE_STATE_COPY_DEST as required.
        texParams.name = L"HeightMap";
        tile.heightMap = renderer.CreateTexture2D(texParams);
        tile.intermediateBuffer = renderer.CreateTexture2DUploadBuffer(texParams);
        heightMaps[i] = tile.heightMap.Get();

        texParams.format = NormalMapTexFormat;
        texParams.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        texParams.initialState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; // We'll initalise this with compute.
        texParams.name = L"NormalMap";
        tile.normalMap = renderer.CreateTexture2D(texParams);
        normalMaps[i] = tile.normalMap.Get();
    }

    m_baseHeightMapTexIndex = renderer.AllocateTex2DSRVs((int)std::size(heightMaps), heightMaps, HeightmapTexFormat);
    m_baseNormalMapTexIndex = renderer.AllocateTex2DSRVs((int)std::size(normalMaps), normalMaps, NormalMapTexFormat);

    renderer.WaitUploads(renderer.EndUploads());

    // Must be done after upload has completed!
    // TODO: Expose m_d3d12CommandQueue->Wait(other.m_d3d12Fence.Get(), other.m_FenceValue)
    //       via some kind of CommandQueue::GPUWait() interface!
    m_intermediateDiffuseTexBuffers[0] = nullptr;
    m_intermediateDiffuseTexBuffers[1] = nullptr;
    renderer.GenerateMips(m_diffuseTextures[0].Get());
    renderer.GenerateMips(m_diffuseTextures[1].Get());
    renderer.GenerateMips(m_detailNormalMaps[0].Get());
    renderer.GenerateMips(m_detailNormalMaps[1].Get());

    return LoadCompiledShaders(renderer);
}

void Terrain::Build(Renderer& renderer)
{
    // Ensure offset is up to date.
    m_clipmapTexelOffset = CalcClipmapTexelOffset(renderer.GetCamPos());

    renderer.BeginUploads();

    // Note: rand() is not seeded so this is still deterministic, for now.
    if (m_randomiseSeed)
    {
        m_seed = rand();
    }

    BuildVertexBuffer(renderer);
    BuildIndexBuffer(renderer);
    BuildWater(renderer);

    m_uploadFenceVal = renderer.EndUploads();

    // NOTE: Heightmap textures are uploaded using the compute queue, not the copy queue.
    // This is because we immediately want to compute normals after uploading.
    // It's probably not optimal, but this way we don't have to make the compute queue wait for the copy queue to start doing that.
    renderer.BeginCompute();

    // Generate clipmap height data and compute normals.
    for (int level = 0; level < NumClipLevels; ++level)
    {
        UpdateClipmapTextureLevel(renderer, level, -Vec2i(INT_MAX, INT_MAX) / 2, m_clipmapTexelOffset);
    }

    m_computeFenceVal = renderer.EndCompute();
}

void Terrain::Render(Renderer& renderer)
{
    if (!m_freezeClipmap)
    {
        UpdateClipmapTextures(renderer);
    }

    // Update shader UV offset.
    m_mappedConstantBuffers[renderer.GetCurrentBuffer()]->clipmapUVOffset = Vec2f(m_clipmapTexelOffset) / (float)HeightmapDimension;

    // Wait for any pending uploads.
    if (m_uploadFenceVal != 0)
    {
        renderer.WaitUploads(m_uploadFenceVal);
        m_uploadFenceVal = 0;
    }

    if (m_computeFenceVal != 0)
    {
        renderer.WaitCompute(m_computeFenceVal);
        m_computeFenceVal = 0;
    }

    ID3D12GraphicsCommandList& commandList = renderer.GetDirectCommandList();

    if (m_detailTexStateDirty)
    {
        // After creating the textures, we should transition them to the shader resource states for efficiency.
        D3D12_RESOURCE_BARRIER barriers[2 * NumDetailTextureSets] = {};
        for (int i = 0; i < NumDetailTextureSets; ++i)
        {
            barriers[2 * i + 0] = CD3DX12_RESOURCE_BARRIER::Transition(m_diffuseTextures[i].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            barriers[2 * i + 1] = CD3DX12_RESOURCE_BARRIER::Transition(m_detailNormalMaps[i].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        commandList.ResourceBarrier((int)std::size(barriers), barriers);

        m_detailTexStateDirty = false;
    }

    // Set PSO/shader state
    commandList.SetPipelineState(m_pipelineState.Get());
    renderer.BindDescriptor(m_cbufferDescIndex, RootParam::PSConstantBuffer);
    renderer.BindDescriptor(m_baseHeightMapTexIndex, RootParam::VertexTexture0);
    renderer.BindDescriptor(m_baseNormalMapTexIndex, RootParam::VertexTexture1);
    renderer.BindDescriptor(m_diffuseTexDescIndices[0], RootParam::Texture0);
    renderer.BindDescriptor(m_diffuseTexDescIndices[1], RootParam::Texture1);
    renderer.BindDescriptor(m_normalTexDescIndices[0], RootParam::Texture2);
    renderer.BindDescriptor(m_normalTexDescIndices[1], RootParam::Texture3);
    renderer.BindSampler(m_heightmapSamplerDescIndex);

    // Render the terrain itself.
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    commandList.IASetVertexBuffers(0, 1, &m_vertexBuffer.view);
    commandList.IASetIndexBuffer(&m_indexBuffer.view);
    commandList.DrawIndexedInstanced(IndexBufferLength, 1, 0, 0, 0);

    // Render "water".
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList.SetPipelineState(m_waterPipelineState.Get());
    commandList.IASetVertexBuffers(0, 1, &m_waterVertexBuffer.view);
    commandList.IASetIndexBuffer(&m_waterIndexBuffer.view);
    commandList.DrawIndexedInstanced(m_waterIndexBuffer.view.SizeInBytes / sizeof(uint16), 1, 0, 0, 0);
}

void Terrain::UpdateClipmapTextures(Renderer& renderer)
{
    Vec2i newTexelOffset = CalcClipmapTexelOffset(renderer.GetCamPos());

    if (m_clipmapTexelOffset == newTexelOffset && m_globalDirtyRegionMin.x >= m_globalDirtyRegionMax.x && m_globalDirtyRegionMin.y >= m_globalDirtyRegionMax.y)
        return;

    renderer.WaitCurrentFrame();
    renderer.BeginCompute();
    ID3D12GraphicsCommandList& commandList = renderer.GetComputeCommandList();

    // Update modified region, if any.
    Vec2i globalDirtyRegionMin = m_globalDirtyRegionMin;
    Vec2i globalDirtyRegionMax = m_globalDirtyRegionMax;
    m_globalDirtyRegionMin = Vec2iZero;
    m_globalDirtyRegionMax = Vec2iZero;
    if (globalDirtyRegionMin.x < globalDirtyRegionMax.x && globalDirtyRegionMin.y < globalDirtyRegionMax.y)
    {
        for (int level = 0; level < NumClipLevels; ++level)
        {
            UploadClipmapTextureRegion(renderer, level, globalDirtyRegionMin, globalDirtyRegionMax, newTexelOffset);
        }
    }

    // Update heightmap textures.
    if (m_clipmapTexelOffset != newTexelOffset)
    {
        for (int level = 0; level < NumClipLevels; ++level)
        {
            UpdateClipmapTextureLevel(renderer, level, m_clipmapTexelOffset, newTexelOffset);
        }
    }

    m_computeFenceVal = renderer.EndCompute();

    m_clipmapTexelOffset = newTexelOffset;
}
 
void Terrain::UpdateClipmapTextureLevel(Renderer& renderer, int level, Vec2i oldTexelOffset, Vec2i newTexelOffset)
{
    // Update the clipmap at the given level based on camera movement.
    // Texturing is done toroidally, so when the camera moves we replace the furthest slice
    // of the texture in the direction we are moving away from with the new slice we need.
    // In most cases this will be a single horizontal or vertical slice, but in general
    // it will be both (i.e. a cross) or up to four slices if the dirty region wraps
    // across the edge of the texture. A good overview of clipmaps:
    // https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry

    // Check we've moved far enough to trigger an update at this level.
    oldTexelOffset >>= level;
    newTexelOffset >>= level;
    if (oldTexelOffset == newTexelOffset)
        return;

    // Calculate dirty region in texel space for this clip level.
    // All "regions" in this function actually represent a cross in the texture
    // as described above.
    const Vec2i fullSize(HeightmapDimension, HeightmapDimension);
    const Vec2i halfSize = fullSize / 2;
    Vec2i deltaSign = math::sign(newTexelOffset - oldTexelOffset);
    Vec2i worldUploadRegionMin = oldTexelOffset + halfSize + deltaSign * halfSize;
    Vec2i worldUploadRegionMax = newTexelOffset + halfSize + deltaSign * halfSize;
    for (int i = 0; i < 2; ++i)
    {
        // Ensure min < max and limit the upload in case we teleported more than the width of the whole texture.
        if (worldUploadRegionMax[i] < worldUploadRegionMin[i])
        {
            std::swap(worldUploadRegionMin[i], worldUploadRegionMax[i]);
            worldUploadRegionMax[i] = std::min(worldUploadRegionMax[i], worldUploadRegionMin[i] + HeightmapDimension);
        }
        else
        {
            worldUploadRegionMin[i] = std::max(worldUploadRegionMin[i], worldUploadRegionMax[i] - HeightmapDimension);
        }
    }

    // Calculate the whole world texel region that we want for the clip level.
    Vec2i wantRegionMin = newTexelOffset;
    Vec2i wantRegionMax = newTexelOffset + HeightmapSize;

    // Map the intermediate buffer for the height map.
    ClipmapLevel& levelData = m_clipmapLevels[level];
    float* mappedHeights = nullptr;
    levelData.intermediateBuffer->Map(0, nullptr, (void**)&mappedHeights);
    Assert(mappedHeights);

    // Write the two (wrapped) quads we need to update to the mapped buffer.
    if ((worldUploadRegionMax.x - worldUploadRegionMin.x == HeightmapDimension) || (worldUploadRegionMax.y - worldUploadRegionMin.y == HeightmapDimension))
    {
        // If we need to copy the whole texture, just do it once.
        WriteIntermediateTextureData(mappedHeights, level, wantRegionMin, wantRegionMax);
    }
    else
    {
        // Else copy the two (wrapping) slices.
        WriteIntermediateTextureData(mappedHeights, level, Vec2i(worldUploadRegionMin.x, wantRegionMin.y), Vec2i(worldUploadRegionMax.x, wantRegionMax.y));
        WriteIntermediateTextureData(mappedHeights, level, Vec2i(wantRegionMin.x, worldUploadRegionMin.y), Vec2i(wantRegionMax.x, worldUploadRegionMax.y));
    }

    // Calculate the region of the texture we wrote to (possibly wrapping across the edge).
    Vec2i texUploadRegionMin(WrapHeightmapCoords(worldUploadRegionMin));
    Vec2i texUploadRegionMax(WrapHeightmapCoords(worldUploadRegionMax));

    // Unmap intermediate buffer.
    if (texUploadRegionMin.x == texUploadRegionMax.x && texUploadRegionMin.y < texUploadRegionMax.y)
    {
        // No vertical slice and no vertical wrap so only flush the rows we touched.
        D3D12_RANGE writeRange = { (size_t)HeightmapIndex(0, texUploadRegionMin.y), (size_t)HeightmapIndex(HeightmapDimension - 1, texUploadRegionMax.y) + 1 };
        levelData.intermediateBuffer->Unmap(0, &writeRange);
    }
    else
    {
        // Flush whole range because we had to touch either every row or at least the top and bottom row.
        levelData.intermediateBuffer->Unmap(0, nullptr);
    }

    // Copy from the intermediate buffer to the actual texture.
    D3D12_TEXTURE_COPY_LOCATION heightDst = MakeDstTexCopyLocation(levelData.heightMap.Get());
    D3D12_TEXTURE_COPY_LOCATION heightSrc = MakeSrcTexCopyLocation(levelData.intermediateBuffer.Get(), HeightmapTexFormat);
    ID3D12GraphicsCommandList& commandList = renderer.GetComputeCommandList();

    D3D12_RESOURCE_BARRIER preBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_clipmapLevels[level].heightMap.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    commandList.ResourceBarrier(1, &preBarrier);

    auto CopyBox = [&](Vec2i minInclusive, Vec2i maxExclusive) { CopyTex2DRegion(commandList, heightDst, heightSrc, minInclusive, maxExclusive); };

    if (worldUploadRegionMin.x + HeightmapDimension == worldUploadRegionMax.x || worldUploadRegionMin.y + HeightmapDimension == worldUploadRegionMax.y)
    {
        // Copy the whole thing because we moved the whole width or height of the texture.
        CopyBox(Vec2iZero, HeightmapSize);
    }
    else
    {
        // Copy vertical slice(s).
        if (texUploadRegionMin.x < texUploadRegionMax.x)
        {
            CopyBox(Vec2i(texUploadRegionMin.x, 0), Vec2i(texUploadRegionMax.x + 1, HeightmapDimension));
        }
        else if (texUploadRegionMin.x > texUploadRegionMax.x)
        {
            // Dirty region wraps across the boundary so copy two slices.
            CopyBox(Vec2i(texUploadRegionMin.x, 0), Vec2i(HeightmapDimension, HeightmapDimension));
            CopyBox(Vec2i(0, 0), Vec2i(texUploadRegionMax.x + 1, HeightmapDimension));
        }

        // Copy horizontal slice(s).
        if (texUploadRegionMin.y < texUploadRegionMax.y)
        {
            CopyBox(Vec2i(0, texUploadRegionMin.y), Vec2i(HeightmapDimension, texUploadRegionMax.y + 1));
        }
        else if (texUploadRegionMin.y > texUploadRegionMax.y)
        {
            // Dirty region wraps across the boundary so copy two slices.
            CopyBox(Vec2i(0, texUploadRegionMin.y), Vec2i(HeightmapDimension, HeightmapDimension));
            CopyBox(Vec2i(0, 0), Vec2i(HeightmapDimension, texUploadRegionMax.y + 1));
        }
    }

    D3D12_RESOURCE_BARRIER postBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_clipmapLevels[level].heightMap.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList.ResourceBarrier(1, &postBarrier);

    // Update normal map. The compute shader can wrap around so always just two dispatches for this.
    auto ComputeNormals = [&](Vec2i min, Vec2i max)
    {
        // Pad the region by 1 cell in each direction since height affects adjacent normals.
        Vec2i normalMin = min - Vec2i(1, 1);
        Vec2i normalMax = max + Vec2i(1, 1);
        m_computeNormals->Compute(renderer, m_clipmapLevels[level].heightMap.Get(), m_clipmapLevels[level].normalMap.Get(), normalMin, normalMax, level);
    };

    // Pass world UVs into compute since it does the wrapping for us and relies on min < max.
    ComputeNormals(Vec2i(worldUploadRegionMin.x, 0), Vec2i(worldUploadRegionMax.x + 1, HeightmapDimension)); // Vertical slice
    ComputeNormals(Vec2i(0, worldUploadRegionMin.y), Vec2i(HeightmapDimension, worldUploadRegionMax.y + 1)); // Horizontal slice
}

void Terrain::UploadClipmapTextureRegion(Renderer& renderer, int level, Vec2i globalMin, Vec2i globalMax, Vec2i newTexelOffset)
{
    // Upload a dirty AABB region (inclusive bounds) of a single clipmap level, after terrain has been modified.
    // Note that the logic in this function is different to UpdateHeightmapTextureLevel();
    // here we are dealing with a single AABB, whereas that function must upload a cross that spans the whole clipmap texture.

    Assert(globalMin.x <= globalMax.x);
    Assert(globalMin.y <= globalMax.y);
    Vec2i levelGlobalMin = globalMin >> level;
    Vec2i levelGlobalMax = globalMax >> level;

    // Transform to a [min, max) region now that we're done shifting for levels.
    levelGlobalMax += Vec2i(1, 1);

    // Offset to account for clipmap tiling origin.
    // TODO: this is getting ridiculous, can I remove this?
    const Vec2i fullSize(HeightmapDimension, HeightmapDimension);
    const Vec2i halfSize = fullSize / 2;
    levelGlobalMin += halfSize;
    levelGlobalMax += halfSize;

    // Does the dirty region actually overlap with the active texture region at this clip level?
    Vec2i textureRegionMin = (newTexelOffset >> level);
    Vec2i textureRegionMax = (newTexelOffset >> level) + fullSize;
    levelGlobalMin = std::clamp(levelGlobalMin, textureRegionMin, textureRegionMax);
    levelGlobalMax = std::clamp(levelGlobalMax, textureRegionMin, textureRegionMax);
    if (levelGlobalMin.x == levelGlobalMax.x || levelGlobalMin.y == levelGlobalMax.y)
        return;

    // Map the intermediate buffer.
    ClipmapLevel& levelData = m_clipmapLevels[level];
    float* mappedData = nullptr;
    levelData.intermediateBuffer->Map(0, nullptr, (void**)&mappedData);
    Assert(mappedData);

    // Copy tile data to the intermediate buffer.
    WriteIntermediateTextureData(mappedData, level, levelGlobalMin, levelGlobalMax);

    // Calculate the region of the texture we will write to (possibly wrapping across the edge).
    Vec2i texUploadRegionMin(WrapHeightmapCoords(levelGlobalMin));
    Vec2i texUploadRegionMax(WrapHeightmapCoords(levelGlobalMax));

    // Unmap intermediate buffer.
    if (texUploadRegionMin.y < texUploadRegionMax.y)
    {
        // No vertical wrap so only flush the rows we touched.
        D3D12_RANGE writeRange = { (size_t)HeightmapIndex(0, texUploadRegionMin.y), (size_t)HeightmapIndex(HeightmapDimension - 1, texUploadRegionMax.y) + 1 };
        levelData.intermediateBuffer->Unmap(0, &writeRange);
    }
    else
    {
        // Flush whole range because we had to touch either every row or at least the top and bottom row.
        levelData.intermediateBuffer->Unmap(0, nullptr);
    }

    // Copy from the intermediate buffer to the actual texture.
    D3D12_TEXTURE_COPY_LOCATION dst = MakeDstTexCopyLocation(levelData.heightMap.Get());
    D3D12_TEXTURE_COPY_LOCATION src = MakeSrcTexCopyLocation(levelData.intermediateBuffer.Get(), HeightmapTexFormat);
    ID3D12GraphicsCommandList& commandList = renderer.GetComputeCommandList();
    auto CopyBox = [&](Vec2i minInclusive, Vec2i maxExclusive)
    {
        Assert(minInclusive.y < maxExclusive.y);
        CopyTex2DRegion(commandList, dst, src, minInclusive, maxExclusive);
    };

    D3D12_RESOURCE_BARRIER preBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_clipmapLevels[level].heightMap.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    commandList.ResourceBarrier(1, &preBarrier);

    if (texUploadRegionMin.x < texUploadRegionMax.x)
    {
        if (texUploadRegionMin.y < texUploadRegionMax.y)
        {
            // The region doesn't wrap either boundary so just do a single copy.
            CopyBox(texUploadRegionMin, texUploadRegionMax + Vec2i(1, 1));
        }
        else
        {
            // Dirty region wraps across the boundary so copy two regions.
            CopyBox(Vec2i(texUploadRegionMin.x, 0), texUploadRegionMax + Vec2i(1, 1));
            CopyBox(texUploadRegionMin, Vec2i(texUploadRegionMax.x + 1, HeightmapDimension));
        }
    }
    else
    {
        if (texUploadRegionMin.y < texUploadRegionMax.y)
        {
            // Dirty region wraps across the boundary so copy two regions.
            CopyBox(Vec2i(0, texUploadRegionMin.y), texUploadRegionMax + Vec2i(1, 1));
            CopyBox(texUploadRegionMin, Vec2i(HeightmapDimension, texUploadRegionMax.y + 1));
        }
        else
        {
            // Dirty region wraps across *both* boundaries so copy four regions!
            CopyBox(texUploadRegionMin, fullSize);
            CopyBox(Vec2i(texUploadRegionMin.x, 0), Vec2i(HeightmapDimension, texUploadRegionMax.y + 1));
            CopyBox(Vec2i(0, texUploadRegionMin.y), Vec2i(texUploadRegionMax.x + 1, HeightmapDimension));
            CopyBox(Vec2iZero, texUploadRegionMax + Vec2i(1, 1));
        }
    }

    D3D12_RESOURCE_BARRIER postBarrier = CD3DX12_RESOURCE_BARRIER::Transition(m_clipmapLevels[level].heightMap.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    commandList.ResourceBarrier(1, &postBarrier);

    // Update normal map. The compute shader can wrap around so always just a single dispatch for this.
    // Pad the region by 1 cell in each direction since height affects adjacent normals.
    // Pass world UVs into compute since it does the wrapping for us and relies on min < max.
    Vec2i normalMin = levelGlobalMin - Vec2i(1, 1);
    Vec2i normalMax = levelGlobalMax + Vec2i(1, 1);
    m_computeNormals->Compute(renderer, m_clipmapLevels[level].heightMap.Get(), m_clipmapLevels[level].normalMap.Get(), normalMin, normalMax, level);
}

Terrain::HeightmapData& Terrain::GetOrCreateTile(Vec2i tile, int level)
{
    auto [it, inserted] = m_tileCaches[level].try_emplace(tile, HeightmapData());
    HeightmapData& heightmap = it->second;
    if (inserted)
    {
        // We have to initialise this tile. Fill in the noise data.
        Vec2i tileBaseCoords = tile * TileDimension;
        heightmap.reserve(math::Square(TileDimension));
        for (int z = 0; z < TileDimension; ++z)
        {
            for (int x = 0; x < TileDimension; ++x)
            {
                heightmap.push_back(GenerateHeight(tileBaseCoords + Vec2i(x, z), level));
            }
        }
    }
    return heightmap;
}

void Terrain::RaiseAreaRounded(Renderer& renderer, Vec2f posXZ, float radius, float raiseBy)
{
    // Check buffers not already being uploaded.
    Assert(m_uploadFenceVal == 0);

    // Find all tiles touched by this transform.
    // Account for tile borders.
    Vec2i minGlobalCoords = WorldPosToGlobalCoords(posXZ - Vec2f(radius, radius));
    Vec2i maxGlobalCoords = WorldPosToGlobalCoords(posXZ + Vec2f(radius, radius));
    Vec2i minTile = GlobalCoordsToTile(minGlobalCoords - Vec2i(TexelSize, TexelSize), 0).first;
    Vec2i maxTile = GlobalCoordsToTile(maxGlobalCoords + Vec2i(TexelSize, TexelSize), 0).first;

    for (int tileZ = minTile.y; tileZ <= maxTile.y; ++tileZ)
    {
        for (int tileX = minTile.x; tileX <= maxTile.x; ++tileX)
        {
            Vec2i tile(tileX, tileZ);
            HeightmapData& heightmap = GetOrCreateTile(tile, 0);

            // Find bounds within the tile that could be touched.
            Vec2i minVert = GlobalCoordsToTileCoords(minGlobalCoords, tile, 0);
            Vec2i maxVert = GlobalCoordsToTileCoords(maxGlobalCoords, tile, 0) + Vec2i(1, 1);

            // Clamp to tile bounds.
            minVert.x = std::clamp(minVert.x, 0, TileDimension);
            minVert.y = std::clamp(minVert.y, 0, TileDimension);
            maxVert.x = std::clamp(maxVert.x, 0, TileDimension);
            maxVert.y = std::clamp(maxVert.y, 0, TileDimension);

            // Update heightmap.
            for (int z = minVert.y; z < maxVert.y; ++z)
            {
                for (int x = minVert.x; x < maxVert.x; ++x)
                {
                    int globalX = x + tileX * TileDimension;
                    int globalZ = z + tileZ * TileDimension;
                    Vec2f pos = GlobalCoordsToWorldPos(Vec2i(globalX, globalZ));
                    float distSq = math::length2(pos - posXZ);
                    heightmap[TileIndex(x, z)] += raiseBy * std::max(math::Square(radius) - distSq, 0.f);
                }
            }
        }
    }

    // Update lower tile mips.
    for (int level = 1; level < NumClipLevels; ++level)
    {
        Vec2i levelGlobalMin = minGlobalCoords >> level;
        Vec2i levelGlobalMax = (maxGlobalCoords >> level) + Vec2i(1, 1);
        
        // Could also loop over tiles here to avoid repeated GetOrCreateTile() calls...
        // But the hashmap lookup should be practically free most of the time, so doesn't seem worth the code bloat.
        for (int z = levelGlobalMin.y; z < levelGlobalMax.y; ++z)
        {
            for (int x = levelGlobalMin.x; x < levelGlobalMax.x; ++x)
            {
                Vec2i levelGlobalCoords(x, z);
                auto [dstTile, dstTileCoords] = LevelGlobalCoordsToTile(levelGlobalCoords);
                auto [srcTile, srcTileCoords] = LevelGlobalCoordsToTile(levelGlobalCoords << 1);
                const HeightmapData& srcHeightmap = GetOrCreateTile(srcTile, level - 1);
                HeightmapData& dstHeightmap = GetOrCreateTile(dstTile, level);
                dstHeightmap[TileIndex(dstTileCoords)] = 0.25f * (srcHeightmap[TileIndex(srcTileCoords)] +
                                                                  srcHeightmap[TileIndex(srcTileCoords + Vec2i(1, 0))] +
                                                                  srcHeightmap[TileIndex(srcTileCoords + Vec2i(0, 1))] +
                                                                  srcHeightmap[TileIndex(srcTileCoords + Vec2i(1, 1))]);
            }
        }
    }

    // Flag clipmap as dirty.
    Assert(m_globalDirtyRegionMin == Vec2iZero && m_globalDirtyRegionMax == Vec2iZero);
    m_globalDirtyRegionMin = minGlobalCoords;
    m_globalDirtyRegionMax = maxGlobalCoords;
}

bool Terrain::LoadCompiledShaders(Renderer& renderer)
{
    // Production: load precompiled shaders
    ComPtr<ID3DBlob> vertexShader = renderer.LoadCompiledShader(L"TerrainVertex.cso");
    ComPtr<ID3DBlob> hullShader = renderer.LoadCompiledShader(L"TerrainHull.cso");
    ComPtr<ID3DBlob> domainShader = renderer.LoadCompiledShader(L"TerrainDomain.cso");
    ComPtr<ID3DBlob> pixelShader = renderer.LoadCompiledShader(L"TerrainPixel.cso");
    if (!(vertexShader && hullShader && domainShader && pixelShader))
        return false;

    if (!CreatePipelineState(renderer, vertexShader.Get(), hullShader.Get(), domainShader.Get(), pixelShader.Get()))
        return false;

    
    // Load water shaders
    ComPtr<ID3DBlob> waterVertexShader = renderer.LoadCompiledShader(L"WaterVertex.cso");
    ComPtr<ID3DBlob> waterPixelShader = renderer.LoadCompiledShader(L"WaterPixel.cso");
    if (!(waterVertexShader && waterPixelShader))
        return false;

    return CreateWaterPipelineState(renderer, waterVertexShader.Get(), waterPixelShader.Get());
}

bool Terrain::HotloadShaders(Renderer& renderer)
{
    // Development: compile from files on the fly
    ComPtr<ID3DBlob> vertexShader = renderer.CompileShader(L"TerrainVertex.hlsl", ShaderStage::Vertex);
    ComPtr<ID3DBlob> hullShader = renderer.CompileShader(L"TerrainHull.hlsl", ShaderStage::Hull);
    ComPtr<ID3DBlob> domainShader = renderer.CompileShader(L"TerrainDomain.hlsl", ShaderStage::Domain);
    ComPtr<ID3DBlob> pixelShader = renderer.CompileShader(L"TerrainPixel.hlsl", ShaderStage::Pixel);
    if (!(vertexShader && hullShader && domainShader && pixelShader))
        return false;

    // Force a full CPU/GPU sync then recreate the PSO.
    renderer.WaitCurrentFrame();

    if (!CreatePipelineState(renderer, vertexShader.Get(), hullShader.Get(), domainShader.Get(), pixelShader.Get()))
        return false;

    ComPtr<ID3DBlob> waterVertexShader = renderer.CompileShader(L"WaterVertex.hlsl", ShaderStage::Vertex);
    ComPtr<ID3DBlob> waterPixelShader = renderer.CompileShader(L"WaterPixel.hlsl", ShaderStage::Pixel);
    if (!(waterVertexShader && waterPixelShader))
        return false;


    return CreateWaterPipelineState(renderer, waterVertexShader.Get(), waterPixelShader.Get());
}

void Terrain::Imgui(Renderer& renderer)
{
    if (ImGui::Begin("Terrain"))
    {
        ImGui::DragFloat("Base Height", &m_baseHeight, 0.01f, -20.f, 20.f);

        auto noiseParams = [](auto& params, int baseID)
        {
            for (int i = 0; i < (int)std::size(params); ++i)
            {
                ImGui::Columns(2);
                ImGui::PushID(baseID | i);
                ImGui::DragFloat("Frequency", &params[i].frequency, 0.0002f, 0.f, 0.1f, "%0.4f");
                ImGui::NextColumn();
                ImGui::DragFloat("Amplitude", &params[i].amplitude, 0.01f, -10.f, 30.f);
                ImGui::NextColumn();
                ImGui::PopID();
            }

            ImGui::Columns(1);
        };

        if (ImGui::CollapsingHeader("Ridge Noise"))
        {
            noiseParams(m_ridgeNoiseParams, 0);
        }

        if (ImGui::CollapsingHeader("Ridge Noise Multiplier"))
        {
            noiseParams(m_ridgeNoiseMultiplierParams, 0x4000);
        }

        if (ImGui::CollapsingHeader("White Noise"))
        {
            noiseParams(m_whiteNoiseParams, 0x8000);
        }

        if (ImGui::CollapsingHeader("Coordinates"))
        {
            Vec2f cursorPos = m_mappedConstantBuffers[0]->highlightPosXZ;
            Vec2i globalCoords = WorldPosToGlobalCoords(cursorPos);
            auto [tile, tileCoords] = WorldPosToTile(cursorPos, 0);
            ImGui::Text("Cursor Pos:    (%.2f, %.2f)", cursorPos.x, cursorPos.y);
            ImGui::Text("Global Coords: (%02d, %02d)", globalCoords.x, globalCoords.y);
            ImGui::Text("Tile:          (%02d, %02d)", tile.x, tile.y);
            ImGui::Text("Tile Coords:   (%02d, %02d)", tileCoords.x, tileCoords.y);
        }
        
        if (ImGui::Button("Regenerate"))
        {
            renderer.WaitCurrentFrame();
            Build(renderer);
        }

        ImGui::SameLine();
        ImGui::Checkbox("Randomise Seed", &m_randomiseSeed);
        ImGui::Checkbox("Freeze Clipmap", &m_freezeClipmap);

        if (ImGui::Checkbox("Wireframe Mode", &m_wireframeMode))
        {
            // Trigger PSO recreation.
            HotloadShaders(renderer);
        }

        if (ImGui::Button("Reload Shaders"))
        {
            HotloadShaders(renderer);
        }
    }
    ImGui::End();
}

bool Terrain::CreatePipelineState(Renderer& renderer, ID3DBlob* vertexShader, ID3DBlob* hullShader, ID3DBlob* domainShader, ID3DBlob* pixelShader)
{
    // Create PSO
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT inputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY primType;
        CD3DX12_PIPELINE_STATE_STREAM_VS vs;
        CD3DX12_PIPELINE_STATE_STREAM_HS hs;
        CD3DX12_PIPELINE_STATE_STREAM_DS ds;
        CD3DX12_PIPELINE_STATE_STREAM_PS ps;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT dsvFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtvFormats;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC blend;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
    };

    // Define vertex layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_RT_FORMAT_ARRAY rtvFormats = {};
    rtvFormats.NumRenderTargets = 1;
    rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

    PipelineStateStream pipelineStateStream;
    pipelineStateStream.rootSignature = &renderer.GetRootSignature();
    pipelineStateStream.inputLayout = { inputLayout, (UINT)std::size(inputLayout) };
    pipelineStateStream.primType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    pipelineStateStream.vs = CD3DX12_SHADER_BYTECODE(vertexShader);
    pipelineStateStream.hs = CD3DX12_SHADER_BYTECODE(hullShader);
    pipelineStateStream.ds = CD3DX12_SHADER_BYTECODE(domainShader);
    pipelineStateStream.ps = CD3DX12_SHADER_BYTECODE(pixelShader);
    pipelineStateStream.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineStateStream.rtvFormats = rtvFormats;
    ((CD3DX12_RASTERIZER_DESC&)pipelineStateStream.rasterizer).FrontCounterClockwise = true;

    if (m_wireframeMode)
    {
        ((CD3DX12_RASTERIZER_DESC&)pipelineStateStream.rasterizer).FillMode = D3D12_FILL_MODE_WIREFRAME;
    }

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = { sizeof(PipelineStateStream), &pipelineStateStream };
    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(renderer.GetDevice().CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&pso))))
    {
        DebugOut("Failed to create pipeline state object!\n");
        return false;
    }

    m_pipelineState = pso;
    return true;
}

bool Terrain::CreateWaterPipelineState(Renderer& renderer, ID3DBlob* vertexShader, ID3DBlob* pixelShader)
{
    // Create PSO
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT inputLayout;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY primType;
        CD3DX12_PIPELINE_STATE_STREAM_VS vs;
        CD3DX12_PIPELINE_STATE_STREAM_PS ps;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT dsvFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtvFormats;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC blend;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
    };

    // Define vertex layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOUR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
    ((CD3DX12_RASTERIZER_DESC&)pipelineStateStream.rasterizer).FrontCounterClockwise = true;

    // Enable blending.
    D3D12_RENDER_TARGET_BLEND_DESC& rtBlendDesc = ((CD3DX12_BLEND_DESC&)pipelineStateStream.blend).RenderTarget[0];
    rtBlendDesc.BlendEnable = true;
    rtBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rtBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = { sizeof(PipelineStateStream), &pipelineStateStream };
    if (FAILED(renderer.GetDevice().CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_waterPipelineState))))
    {
        DebugOut("Failed to create pipeline state object!\n");
        return false;
    }

    return true;
}

void Terrain::CreateConstantBuffers(Renderer& renderer)
{
    // Create a constant buffer for each frame.
    // Only upload buffers, because they're modified every frame.
    for (int i = 0; i < BackbufferCount; ++i)
    {
        m_constantBuffers[i] = renderer.CreateConstantBuffer(sizeof(TerrainPSConstantBuffer));
        Assert(m_constantBuffers[i]);

        D3D12_RANGE readRange = {};
        m_constantBuffers[i]->Map(0, &readRange, (void**)&m_mappedConstantBuffers[i]);
        Assert(m_mappedConstantBuffers[i]);
        memset(m_mappedConstantBuffers[i], 0, sizeof(TerrainPSConstantBuffer));
    }

    // Get offset into the descriptor heaps for the constant buffers.
    ID3D12Resource* cbuffers[] = { m_constantBuffers[0].Get(), m_constantBuffers[1].Get() };
    m_cbufferDescIndex = renderer.AllocateConstantBufferViews(cbuffers, sizeof(TerrainPSConstantBuffer));

    // Set up a custom sampler for the heightmap.
    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    m_heightmapSamplerDescIndex = renderer.AllocateSampler(samplerDesc);
}

void Terrain::BuildIndexBuffer(Renderer& renderer)
{
    size_t dataSize = IndexBufferLength * sizeof(uint16);
    m_indexBuffer.buffer = renderer.CreateResidentBuffer(dataSize);
    m_indexBuffer.intermediateBuffer = renderer.CreateUploadBuffer(dataSize);
    m_indexBuffer.view.BufferLocation = m_indexBuffer.buffer->GetGPUVirtualAddress();
    m_indexBuffer.view.Format = DXGI_FORMAT_R16_UINT;
    m_indexBuffer.view.SizeInBytes = (UINT)dataSize;

    uint16* indexData = nullptr;
    m_indexBuffer.intermediateBuffer->Map(0, nullptr, (void**)&indexData);
    Assert(indexData);

    // Build quad patches.
    for (int z = 0; z < VertexGridDimension; ++z)
    {
        for (int x = 0; x < VertexGridDimension; ++x)
        {
            uint16* p = &indexData[4 * ((VertexGridDimension - 1) * z + x)];
            p[0] = VertexGridDimension * (z + 0) + (x + 0);
            p[1] = VertexGridDimension * (z + 0) + (x + 1);
            p[2] = VertexGridDimension * (z + 1) + (x + 0);
            p[3] = VertexGridDimension * (z + 1) + (x + 1);
        }
    }

    m_indexBuffer.intermediateBuffer->Unmap(0, nullptr);

    ID3D12GraphicsCommandList& commandList = renderer.GetCopyCommandList();
    commandList.CopyBufferRegion(m_indexBuffer.buffer.Get(), 0, m_indexBuffer.intermediateBuffer.Get(), 0, dataSize);
}

void Terrain::BuildVertexBuffer(Renderer& renderer)
{
    size_t dataSize = VertexBufferLength * sizeof(TerrainVertex);
    m_vertexBuffer.buffer = renderer.CreateResidentBuffer(dataSize);
    m_vertexBuffer.intermediateBuffer = renderer.CreateUploadBuffer(dataSize);
    m_vertexBuffer.view.BufferLocation = m_vertexBuffer.buffer->GetGPUVirtualAddress();
    m_vertexBuffer.view.SizeInBytes = (UINT)dataSize;
    m_vertexBuffer.view.StrideInBytes = sizeof(TerrainVertex);

    // Map buffer and fill in vertex data.
    TerrainVertex* vertexData = nullptr;
    m_vertexBuffer.intermediateBuffer->Map(0, nullptr, (void**)&vertexData);
    Assert(vertexData);

    for (int z = 0; z < VertexGridDimension; ++z)
    {
        for (int x = 0; x < VertexGridDimension; ++x)
        {
            TerrainVertex& v = vertexData[VertexIndex(x, z)];
            v.pos = ToVertexPos(x, z);
        }
    }

    m_vertexBuffer.intermediateBuffer->Unmap(0, nullptr);

    // Upload initial data to both buffers.
    ID3D12GraphicsCommandList& commandList = renderer.GetCopyCommandList();
    commandList.CopyBufferRegion(m_vertexBuffer.buffer.Get(), 0, m_vertexBuffer.intermediateBuffer.Get(), 0, dataSize);
}

void Terrain::BuildWater(Renderer& renderer)
{
    const float HalfGridSizeX = 0.5f * VertexPatchSize * (float)(VertexGridDimension - 1);
    const float HalfGridSizeZ = 0.5f * VertexPatchSize * (float)(VertexGridDimension - 1);
    const WaterVertex WaterVerts[] = {
        { { -HalfGridSizeX, 0.f, -HalfGridSizeZ }, Vec3fY, { 0x20, 0x70, 0xff, 0x80 } },
        { { -HalfGridSizeX, 0.f,  HalfGridSizeZ }, Vec3fY, { 0x20, 0x70, 0xff, 0x80 } },
        { {  HalfGridSizeX, 0.f,  HalfGridSizeZ }, Vec3fY, { 0x20, 0x70, 0xff, 0x80 } },
        { {  HalfGridSizeX, 0.f, -HalfGridSizeZ }, Vec3fY, { 0x20, 0x70, 0xff, 0x80 } },
    };

    const uint16 WaterIndices[] = {
        0, 1, 2,
        0, 2, 3
    };

    renderer.CreateBuffer(m_waterVertexBuffer.buffer, m_waterVertexBuffer.intermediateBuffer, sizeof(WaterVerts), WaterVerts);
    m_waterVertexBuffer.view.BufferLocation = m_waterVertexBuffer.buffer->GetGPUVirtualAddress();
    m_waterVertexBuffer.view.SizeInBytes = sizeof(WaterVerts);
    m_waterVertexBuffer.view.StrideInBytes = sizeof(WaterVertex);

    renderer.CreateBuffer(m_waterIndexBuffer.buffer, m_waterIndexBuffer.intermediateBuffer, sizeof(WaterIndices), WaterIndices);
    m_waterIndexBuffer.view.BufferLocation = m_waterIndexBuffer.buffer->GetGPUVirtualAddress();
    m_waterIndexBuffer.view.Format = DXGI_FORMAT_R16_UINT;
    m_waterIndexBuffer.view.SizeInBytes = sizeof(WaterIndices);
}

float Terrain::GetHeight(Vec2i levelGlobalCoords, int level) const
{
    // Check if there is a modification at this position.
    auto [tile, tileCoords] = LevelGlobalCoordsToTile(levelGlobalCoords);
    auto it = m_tileCaches[level].find(tile);
    if (it != m_tileCaches[level].end())
    {
        return it->second[TileIndex(tileCoords.x, tileCoords.y)];
    }

    return GenerateHeight(levelGlobalCoords, level);
}

float Terrain::GenerateHeight(Vec2i levelGlobalCoords, int level) const
{
    // Scale back up to global coords.
    Vec2i globalCoords = (levelGlobalCoords << level);

    // Offset to get the centre of this bigger texel.
    if (level > 1)
    {
        globalCoords += (Vec2i(1, 1) << (level - 1));
    }

    // If not, generate a height from the noise.

    // Additional offset to get the centre of the texel.
    Vec2f fGlobalCoords(globalCoords);
    if (level > 0)
    {
        fGlobalCoords += Vec2f(0.5f, 0.5f);
    }

    float height = m_baseHeight;

    // Offset perlin seeds for each type of noise.
    const int ridgeBaseSeed = 0x1000;
    const int ridgeWhiteBaseSeed = 0x2000;

    // Calculate a multiplier for the ridge noise, based on normal white noise.
    float ridgeNoiseMultiplier = 1.f;
    for (int i = 0; i < (int)std::size(m_ridgeNoiseMultiplierParams); ++i)
    {
        auto [frequency, amplitude] = m_ridgeNoiseMultiplierParams[i];
        ridgeNoiseMultiplier += amplitude * stb_perlin_noise3_seed(globalCoords.x * frequency, 0.f, globalCoords.y * frequency, 0, 0, 0, m_seed + ridgeWhiteBaseSeed + i);
    }
    
    // Do ridge noise to approximate mountain ranges.
    for (int i = 0; i < (int)std::size(m_ridgeNoiseParams); ++i)
    {
        auto [frequency, amplitude] = m_ridgeNoiseParams[i];
        height += ridgeNoiseMultiplier * amplitude * (1.f - fabsf(stb_perlin_noise3_seed(globalCoords.x * frequency, 0.f, globalCoords.y * frequency, 0, 0, 0, m_seed + ridgeBaseSeed + i)));
    }

    // Apply regular white noise on top.
    for (int i = 0; i < (int)std::size(m_whiteNoiseParams); ++i)
    {
        auto [frequency, amplitude] = m_whiteNoiseParams[i];
        height += amplitude * stb_perlin_noise3_seed(globalCoords.x * frequency, 0.f, globalCoords.y * frequency, 0, 0, 0, m_seed + i);
    }

    return height;
}

Vec2f Terrain::ToVertexPos(int globalX, int globalZ)
{
    return Vec2f(
        VertexPatchSize * ((float)globalX - 0.5f * (float)(VertexGridDimension - 1)),
        VertexPatchSize * ((float)globalZ - 0.5f * (float)(VertexGridDimension - 1)));
}

Vec2i Terrain::CalcClipmapTexelOffset(const Vec3f& camPos) const
{
    // Find how many (whole) texels at level 0 in world space we are from the origin.
    // TODO: Store the previous transition direction and fudge to prevent unnecessary jitter.
    return WorldPosToGlobalCoords(Vec2f(camPos.x, camPos.z));
}

void Terrain::WriteIntermediateTextureData(float* mappedHeights, int level, Vec2i levelGlobalMin, Vec2i levelGlobalMax)
{
    // TODO: We don't really need to address this buffer as if it were the actual texture;
    // we could just write to the start of it every time or use a ring buffer.
    // Is a buffer even appropriate or should it be a texture (and use WriteToSubresource instead)?
    for (int z = levelGlobalMin.y; z < levelGlobalMax.y; ++z)
    {
        for (int x = levelGlobalMin.x; x < levelGlobalMax.x; ++x)
        {
            Vec2i levelGlobalCoords = Vec2i(x, z);
            Vec2i texCoords = WrapHeightmapCoords(levelGlobalCoords);

            // Offset input coords back since clipmap tiling is centred at the origin.
            levelGlobalCoords -= Vec2i(HeightmapDimension, HeightmapDimension) / 2;

            int idx = HeightmapIndex(texCoords);
            mappedHeights[idx] = GetHeight(levelGlobalCoords, level);
        }
    }
}

}

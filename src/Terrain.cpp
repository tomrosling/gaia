#include "Terrain.hpp"
#include "Renderer.hpp"

#define STB_PERLIN_IMPLEMENTATION
#include <stb_perlin.h>

namespace gaia
{

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

static constexpr int NumTilesX = 2; // TODO: Tidy up.
static constexpr int NumTilesZ = 2; // 
static constexpr int NumClipLevels = 8;
static constexpr int CellsPerTileX = 255;
static constexpr int CellsPerTileZ = 255;
static constexpr int HeightmapSize = 256;
static constexpr int VertsPerTile = (CellsPerTileX + 1) * (CellsPerTileZ + 1);
static constexpr int IndicesPerTile = 4 * CellsPerTileX * CellsPerTileZ;
static constexpr float TexelSize = 0.05f; // World size of a texel at clip level 0.
static constexpr float CellSize = 0.05f * 64.f; // World size of a vertex patch. TODO: Rename.
static constexpr DXGI_FORMAT HeightmapTexFormat = DXGI_FORMAT_R32_FLOAT;
static_assert(VertsPerTile <= (1 << 16), "Index format too small");
static_assert(HeightmapSize * sizeof(float) == GetTexturePitchBytes(HeightmapSize, sizeof(float)), "Heightmap size does not meet DX12 alignment requirements");


static int VertexIndex(int x, int z) 
{
    Assert(0 <= x && x <= CellsPerTileX);
    Assert(0 <= z && z <= CellsPerTileZ);
    return (CellsPerTileX + 1) * z + x;
}

static int TileIndex(int x, int z)
{
    Assert(0 <= x && x <= NumTilesX);
    Assert(0 <= z && z <= NumTilesZ);
    return (NumTilesX) * z + x;
}

static int HeightmapIndex(int x, int z)
{
    Assert(0 <= x && x < HeightmapSize);
    Assert(0 <= z && z < HeightmapSize);
    return HeightmapSize * z + x;
}

static Vec2i WrapHeightmapCoords(Vec2i levelGlobalCoords)
{
    // Turns a "world" UV coordinate into a local one.
    // This is a mod operation that always returns a positive result.
    static_assert(math::IsPow2(HeightmapSize), "HeightmapSize must be a power of 2");
    return levelGlobalCoords & (HeightmapSize - 1);
}

static Vec2i WorldPosToTile(Vec2f worldPosXZ)
{
    // Account for the world being centred.
    // Probably remove this in future.
    worldPosXZ.x += 0.5f * CellSize * (float)(CellsPerTileX * NumTilesX);
    worldPosXZ.y += 0.5f * CellSize * (float)(CellsPerTileZ * NumTilesZ);

    return Vec2i((int)floorf(worldPosXZ.x / (CellSize * (float)CellsPerTileX)),
                 (int)floorf(worldPosXZ.y / (CellSize * (float)CellsPerTileZ)));
}

static Vec2i WorldPosToCell(Vec2f worldPosXZ, Vec2i tileCoords)
{
    // Account for the world being centred.
    // Probably remove this in future.
    worldPosXZ.x += 0.5f * CellSize * (float)(CellsPerTileX * NumTilesX);
    worldPosXZ.y += 0.5f * CellSize * (float)(CellsPerTileZ * NumTilesZ);

    Vec2i cellPos((int)floorf(worldPosXZ.x / CellSize),
                  (int)floorf(worldPosXZ.y / CellSize));
    cellPos -= Vec2i(tileCoords.x * CellsPerTileX, tileCoords.y * CellsPerTileZ);

    return cellPos;
}


Terrain::Terrain()
    : m_noiseOctaves{ { 0.005f, 3.5f },
                      { 0.02f, 1.0f },
                      { 0.1f, 0.05f },
                      { 0.37f, 0.02f } }
{
}

bool Terrain::Init(Renderer& renderer)
{
    CreateConstantBuffers(renderer);

    renderer.BeginUploads();

    m_texDescIndices[0] = renderer.LoadTexture(m_textures[0], m_intermediateTexBuffers[0], L"aerial_grass_rock_diff_1k.png");
    m_texDescIndices[1] = renderer.LoadTexture(m_textures[1], m_intermediateTexBuffers[1], L"ground_grey_diff_1k.png");

    // Create a set of clipmap textures.
    ID3D12Resource* textures[NumClipLevels] = {};
    m_clipmapLevels.resize(NumClipLevels);
    for (int level = 0; level < NumClipLevels; ++level)
    {
        ClipmapLevel& tile = m_clipmapLevels[level];
        renderer.CreateTexture2D(tile.texture, tile.intermediateBuffer, HeightmapSize, HeightmapSize, HeightmapTexFormat);
        textures[level] = tile.texture.Get();
    }

    m_baseHeightmapTexIndex = renderer.AllocateTex2DSRVs((int)std::size(textures), textures, HeightmapTexFormat);

    renderer.WaitUploads(renderer.EndUploads());

    // Must be done after upload has completed!
    // TODO: Expose m_d3d12CommandQueue->Wait(other.m_d3d12Fence.Get(), other.m_FenceValue)
    //       via some kind of CommandQueue::GPUWait() interface!
    m_intermediateTexBuffers[0] = nullptr;
    m_intermediateTexBuffers[1] = nullptr;
    renderer.GenerateMips(m_textures[0].Get());
    renderer.GenerateMips(m_textures[1].Get());

    return LoadCompiledShaders(renderer);
}

void Terrain::Build(Renderer& renderer)
{
    renderer.BeginUploads();

    // Note: rand() is not seeded so this is still deterministic, for now.
    if (m_randomiseSeed)
    {
        m_seed = rand();
    }

    BuildVertexBuffer(renderer);
    BuildIndexBuffer(renderer);
    BuildWater(renderer);

    for (int level = 0; level < NumClipLevels; ++level)
    {
        BuildHeightmap(renderer, level);
    }

    m_uploadFenceVal = renderer.EndUploads();
}

void Terrain::Render(Renderer& renderer)
{
    if (!m_freezeClipmap)
    {
        UpdateHeightmapTexture(renderer);
    }

    // Update shader UV offset.
    m_mappedConstantBuffers[renderer.GetCurrentBuffer()]->clipmapUVOffset = Vec2f(m_clipmapTexelOffset) / (float)HeightmapSize;

    // Wait for any pending uploads.
    if (m_uploadFenceVal != 0)
    {
        renderer.WaitUploads(m_uploadFenceVal);
        m_uploadFenceVal = 0;
    }

    ID3D12GraphicsCommandList& commandList = renderer.GetDirectCommandList();

    if (m_texStateDirty)
    {
        // After creating the textures, we should transition them to the shader resource states for efficiency.
        for (ComPtr<ID3D12Resource>& tex : m_textures)
        {
            CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                tex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            commandList.ResourceBarrier(1, &barrier);
        }

        m_texStateDirty = false;
    }

    // Set PSO/shader state
    commandList.SetPipelineState(m_pipelineState.Get());
    renderer.BindDescriptor(m_cbufferDescIndex, RootParam::PSConstantBuffer);
    renderer.BindDescriptor(m_baseHeightmapTexIndex, RootParam::VertexTexture0);
    renderer.BindDescriptor(m_texDescIndices[0], RootParam::Texture0);
    renderer.BindDescriptor(m_texDescIndices[1], RootParam::Texture1);
    renderer.BindSampler(m_heightmapSamplerDescIndex);

    // Render the terrain itself.
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    commandList.IASetVertexBuffers(0, 1, &m_vertexBuffer.view);
    commandList.IASetIndexBuffer(&m_indexBuffer.view);
    commandList.DrawIndexedInstanced(IndicesPerTile, 1, 0, 0, 0);

    // Render "water".
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList.SetPipelineState(m_waterPipelineState.Get());
    commandList.IASetVertexBuffers(0, 1, &m_waterVertexBuffer.view);
    commandList.IASetIndexBuffer(&m_waterIndexBuffer.view);
    commandList.DrawIndexedInstanced(m_waterIndexBuffer.view.SizeInBytes / sizeof(uint16), 1, 0, 0, 0);
}

void Terrain::UpdateHeightmapTexture(Renderer& renderer)
{
    Vec3f camPos(math::affineInverse(renderer.GetViewMatrix())[3]);

    auto IFloorDivf = [](float x, float y)
    {
        return (int)floorf(x / y);
    };

    // Find how many (whole) texels at level 0 in world space we are from the origin.
    // TODO: Store the previous transition direction and fudge to prevent unnecessary jitter.
    Vec2i newTexelOffset(IFloorDivf(camPos.x, TexelSize), IFloorDivf(camPos.z, TexelSize));
    if (m_clipmapTexelOffset == newTexelOffset)
        return;

    renderer.WaitCurrentFrame();
    renderer.BeginUploads();

    for (int i = 0; i < NumClipLevels; ++i)
    {
        UpdateHeightmapTextureLevel(renderer, i, newTexelOffset);
    }

    m_uploadFenceVal = renderer.EndUploads();
    m_clipmapTexelOffset = newTexelOffset;
}
 
void Terrain::UpdateHeightmapTextureLevel(Renderer& renderer, int level, Vec2i newTexelOffset)
{
    // Update the clipmap at the given level.
    // Texturing is done toroidally, so when the camera moves we replace the furthest slice
    // of the texture in the direction we are moving away from with the new slice we need.
    // In most cases this will be a single horizontal or vertical slice, but in general
    // it will be both (i.e. a cross) or up to four slices if the dirty region wraps
    // across the edge of the texture. A good overview of clipmaps:
    // https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry

    // Calculate dirty region in (level 0) world texel space.
    // All "regions" in this function actually represent a cross in the texture
    // as described above.
    const Vec2i fullSize(HeightmapSize << level, HeightmapSize << level);
    const Vec2i halfSize = fullSize / 2;
    Vec2i deltaSign = math::sign(newTexelOffset - m_clipmapTexelOffset);
    Vec2i worldUploadRegionMin = m_clipmapTexelOffset + halfSize + deltaSign * halfSize;
    Vec2i worldUploadRegionMax = newTexelOffset       + halfSize + deltaSign * halfSize;
    for (int i = 0; i < 2; ++i)
    {
        if (worldUploadRegionMax[i] < worldUploadRegionMin[i])
        {
            std::swap(worldUploadRegionMin[i], worldUploadRegionMax[i]);
        }

        // Limit the upload in case we teleported more than the width of the whole texture.
        worldUploadRegionMin[i] = std::max(worldUploadRegionMin[i], worldUploadRegionMax[i] - (HeightmapSize << level) + 1);
    }

    // Calculate the whole world texel region that we want for the clip level.
    Vec2i wantRegionMin = newTexelOffset;
    Vec2i wantRegionMax = newTexelOffset + fullSize - Vec2i(1, 1);

    // Calculate the region of the texture we will write to (possibly wrapping across the edge).
    Vec2i texUploadRegionMin(WrapHeightmapCoords(worldUploadRegionMin >> level));
    Vec2i texUploadRegionMax(WrapHeightmapCoords(worldUploadRegionMax >> level));

    // Check we've moved far enough to trigger an update at this level.
    if (texUploadRegionMin == texUploadRegionMax)
        return;

    // Map the intermediate buffer.
    ClipmapLevel& levelData = m_clipmapLevels[level];
    float* mappedData = nullptr;
    D3D12_RANGE readRange = {};
    levelData.intermediateBuffer->Map(0, nullptr, (void**)&mappedData);
    Assert(mappedData);

    // Write the two (wrapped) quads we need to update to the mapped buffer.
    // TODO: We don't really need to address this buffer as if it were the actual texture; 
    // we could just write to the start of it every time or use a ring buffer.
    // Is a buffer even appropriate or should it be a texture (and use WriteToSubresource instead)?
    auto WriteRegion = [this, halfSize](float* mappedData, int level, Vec2i min, Vec2i max)
    {
        // Scale and round down to level-global coords.
        min >>= level;
        max >>= level;

        for (int z = min.y; z <= max.y; ++z)
        {
            for (int x = min.x; x <= max.x; ++x)
            {
                Vec2i levelGlobalCoords(x, z);

                // Scale back up to global coords and offset since tiling is centred at the origin.
                Vec2i globalCoords = (Vec2i(x, z) << level) - halfSize;

                // Calculate local coords.
                Vec2i texCoords = WrapHeightmapCoords(levelGlobalCoords);

                mappedData[HeightmapIndex(texCoords.x, texCoords.y)] = GenerateHeight(globalCoords.x, globalCoords.y);
            }
        }
    };
    WriteRegion(mappedData, level, Vec2i(worldUploadRegionMin.x, wantRegionMin.y), Vec2i(worldUploadRegionMax.x, wantRegionMax.y));
    WriteRegion(mappedData, level, Vec2i(wantRegionMin.x, worldUploadRegionMin.y), Vec2i(wantRegionMax.x, worldUploadRegionMax.y));

    if (texUploadRegionMin.x == texUploadRegionMax.x && texUploadRegionMin.y < texUploadRegionMax.y)
    {
        // No vertical slice and no vertical wrap so only flush the rows we touched.
        D3D12_RANGE writeRange = { (size_t)HeightmapIndex(0, texUploadRegionMin.y), (size_t)HeightmapIndex(HeightmapSize - 1, texUploadRegionMax.y) + 1 };
        levelData.intermediateBuffer->Unmap(0, &writeRange);
    }
    else
    {
        // Flush whole range because we had to touch either every row or at least the top and bottom row.
        levelData.intermediateBuffer->Unmap(0, nullptr);
    }

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = levelData.texture.Get(),
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = levelData.intermediateBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = 0;
    src.PlacedFootprint.Footprint.Format = HeightmapTexFormat;
    src.PlacedFootprint.Footprint.Width = HeightmapSize;
    src.PlacedFootprint.Footprint.Height = HeightmapSize;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = GetTexturePitchBytes(HeightmapSize, sizeof(float));

    // Copy from the intermediate buffer to the actual texture.
    ID3D12GraphicsCommandList& commandList = renderer.GetCopyCommandList();
    auto CopyBox = [&](Vec2i minExclusive, Vec2i maxInclusive)
    {
        D3D12_BOX box;
        box.left = minExclusive.x;
        box.top = minExclusive.y;
        box.front = 0;
        box.right = maxInclusive.x;
        box.bottom = maxInclusive.y;
        box.back = 1;
        commandList.CopyTextureRegion(&dst, box.left, box.top, 0, &src, &box);
    };

    // Copy vertical slice(s).
    if (texUploadRegionMin.x < texUploadRegionMax.x)
    {
        CopyBox(Vec2i(texUploadRegionMin.x, 0), Vec2i(texUploadRegionMax.x + 1, HeightmapSize));
    }
    else if (texUploadRegionMin.x > texUploadRegionMax.x)
    {
        // Dirty region wraps across the boundary so copy two slices.
        CopyBox(Vec2i(texUploadRegionMin.x, 0), Vec2i(HeightmapSize, HeightmapSize));
        CopyBox(Vec2i(0, 0), Vec2i(texUploadRegionMax.x + 1, HeightmapSize));
    }

    // Copy horizontal slice(s).
    if (texUploadRegionMin.y < texUploadRegionMax.y)
    {
        CopyBox(Vec2i(0, texUploadRegionMin.y), Vec2i(HeightmapSize, texUploadRegionMax.y + 1));
    }
    else if (texUploadRegionMin.y > texUploadRegionMax.y)
    {
        // Dirty region wraps across the boundary so copy two slices.
        CopyBox(Vec2i(0, texUploadRegionMin.y), Vec2i(HeightmapSize, HeightmapSize));
        CopyBox(Vec2i(0, 0), Vec2i(HeightmapSize, texUploadRegionMax.y + 1));
    }
}

void Terrain::RaiseAreaRounded(Renderer& renderer, Vec2f posXZ, float radius, float raiseBy)
{
#if 0
    // Check buffers not already being uploaded.
    Assert(m_uploadFenceVal == 0);

    // Find all tiles touched by this transform.
    // Account for tile borders.
    Vec2f minPosXZ = posXZ - Vec2f(radius, radius);
    Vec2f maxPosXZ = posXZ + Vec2f(radius, radius);
    Vec2i minTile = WorldPosToTile(minPosXZ - Vec2f(CellSize, CellSize));
    Vec2i maxTile = WorldPosToTile(maxPosXZ + Vec2f(CellSize, CellSize));

    // Skip if outside the world.
    if (minTile.x >= NumTilesX || minTile.y >= NumTilesZ)
        return;
    if (maxTile.x < 0 || maxTile.y < 0)
        return;

    minTile.x = std::clamp(minTile.x, 0, NumTilesX - 1);
    minTile.y = std::clamp(minTile.y, 0, NumTilesZ - 1);
    maxTile.x = std::clamp(maxTile.x, 0, NumTilesX - 1);
    maxTile.y = std::clamp(maxTile.y, 0, NumTilesZ - 1);

    renderer.BeginUploads();
    ID3D12GraphicsCommandList& commandList = renderer.GetCopyCommandList();

    for (int tileZ = minTile.y; tileZ <= maxTile.y; ++tileZ)
    {
        for (int tileX = minTile.x; tileX <= maxTile.x; ++tileX)
        {
            Tile& tile = m_tiles[TileIndex(tileX, tileZ)];

            // Find bounds within the tile that could be touched.
            Vec2i tileCoords(tileX, tileZ);
            Vec2i minVert = WorldPosToCell(minPosXZ, tileCoords);
            Vec2i maxVert = WorldPosToCell(maxPosXZ, tileCoords);

            // Clamp to heightmap bounds.
            minVert.x = std::clamp(minVert.x, -1, CellsPerTileX + 1);
            minVert.y = std::clamp(minVert.y, -1, CellsPerTileZ + 1);
            maxVert.x = std::clamp(maxVert.x, -1, CellsPerTileX + 1);
            maxVert.y = std::clamp(maxVert.y, -1, CellsPerTileZ + 1);

            // Update heightmap.
            for (int z = minVert.y; z <= maxVert.y; ++z)
            {
                for (int x = minVert.x; x <= maxVert.x; ++x)
                {
                    int globalX = x + tileX * CellsPerTileX;
                    int globalZ = z + tileZ * CellsPerTileZ;
                    Vec2f pos = ToVertexPos(globalX, globalZ);
                    float distSq = math::length2(pos - posXZ);
                    tile.heightmap[HeightmapIndex(x, z)] += raiseBy * std::max(math::Square(radius) - distSq, 0.f);
                }
            }

            // We might not actually have to upload.
            if (maxVert.x < 0 || CellsPerTileX < minVert.x ||
                maxVert.y < 0 || CellsPerTileZ < minVert.y)
                continue;

            // Now clamp to actual tile bounds.
            minVert.x = std::clamp(minVert.x, 0, CellsPerTileX);
            minVert.y = std::clamp(minVert.y, 0, CellsPerTileZ);
            maxVert.x = std::clamp(maxVert.x, 0, CellsPerTileX);
            maxVert.y = std::clamp(maxVert.y, 0, CellsPerTileZ);

            // Last time we modified this tile, we only updated one of the two buffers.
            // Combine the region we modified that time with the one we're modifying this time.
            // This assumes the regions will be close between frames, and performance will be bad if they're not.
            Vec2i dirtyUnionMin(std::min(minVert.x, tile.dirtyMin.x), std::min(minVert.y, tile.dirtyMin.y));
            Vec2i dirtyUnionMax(std::max(maxVert.x, tile.dirtyMax.x), std::max(maxVert.y, tile.dirtyMax.y));
            tile.dirtyMin = minVert;
            tile.dirtyMax = maxVert;

            // Map the intermediate buffer.
            TerrainVertex* vertexData = nullptr;
            D3D12_RANGE readRange = {};
            tile.intermediateBuffer->Map(0, nullptr, (void**)&vertexData);
            Assert(vertexData);

            // Write to mapped buffer, updating normals and colours as we go.
            for (int z = dirtyUnionMin.y; z <= dirtyUnionMax.y; ++z)
            {
                for (int x = dirtyUnionMin.x; x <= dirtyUnionMax.x; ++x)
                {
                    UpdateVertex(vertexData, tile.heightmap, Vec2i(x, z), Vec2i(tileX, tileZ));
                }
            }

            D3D12_RANGE writeRange = {
                VertexIndex(dirtyUnionMin.x, dirtyUnionMin.y) * sizeof(TerrainVertex),
                (VertexIndex(dirtyUnionMax.x, dirtyUnionMax.y) + 1) * sizeof(TerrainVertex)
            };
            tile.intermediateBuffer->Unmap(0, &writeRange);

            tile.currentBuffer ^= 1;
            commandList.CopyBufferRegion(tile.gpuDoubleBuffer[tile.currentBuffer].Get(), writeRange.Begin, 
                tile.intermediateBuffer.Get(), writeRange.Begin, writeRange.End - writeRange.Begin);
        }
    }

    m_uploadFenceVal = renderer.EndUploads();
#endif
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
        if (ImGui::CollapsingHeader("Perlin Octaves", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (int i = 0; i < (int)std::size(m_noiseOctaves); ++i)
            {
                ImGui::Columns(2);
                ImGui::PushID(i);
                ImGui::DragFloat("Frequency", &m_noiseOctaves[i].frequency, 0.001f, 0.f, 1.f);
                ImGui::NextColumn();
                ImGui::DragFloat("Amplitude", &m_noiseOctaves[i].amplitude, 0.01f, 0.f, 10.f);
                ImGui::NextColumn();
                ImGui::PopID();
            }

            ImGui::Columns(1);
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
    size_t dataSize = IndicesPerTile * sizeof(uint16);
    m_indexBuffer.buffer = renderer.CreateResidentBuffer(dataSize);
    m_indexBuffer.intermediateBuffer = renderer.CreateUploadBuffer(dataSize);
    m_indexBuffer.view.BufferLocation = m_indexBuffer.buffer->GetGPUVirtualAddress();
    m_indexBuffer.view.Format = DXGI_FORMAT_R16_UINT;
    m_indexBuffer.view.SizeInBytes = (UINT)dataSize;

    uint16* indexData = nullptr;
    m_indexBuffer.intermediateBuffer->Map(0, nullptr, (void**)&indexData);
    Assert(indexData);

    // Build quad patches.
    for (int z = 0; z < CellsPerTileZ; ++z)
    {
        for (int x = 0; x < CellsPerTileX; ++x)
        {
            uint16* p = &indexData[4 * (CellsPerTileX * z + x)];
            p[0] = (CellsPerTileX + 1) * (z + 0) + (x + 0);
            p[1] = (CellsPerTileX + 1) * (z + 0) + (x + 1);
            p[2] = (CellsPerTileX + 1) * (z + 1) + (x + 0);
            p[3] = (CellsPerTileX + 1) * (z + 1) + (x + 1);
        }
    }

    m_indexBuffer.intermediateBuffer->Unmap(0, nullptr);

    ID3D12GraphicsCommandList& commandList = renderer.GetCopyCommandList();
    commandList.CopyBufferRegion(m_indexBuffer.buffer.Get(), 0, m_indexBuffer.intermediateBuffer.Get(), 0, dataSize);
}

void Terrain::BuildVertexBuffer(Renderer& renderer)
{
    size_t dataSize = VertsPerTile * sizeof(TerrainVertex);
    m_vertexBuffer.buffer = renderer.CreateResidentBuffer(dataSize);
    m_vertexBuffer.intermediateBuffer = renderer.CreateUploadBuffer(dataSize);
    m_vertexBuffer.view.BufferLocation = m_vertexBuffer.buffer->GetGPUVirtualAddress();
    m_vertexBuffer.view.SizeInBytes = (UINT)dataSize;
    m_vertexBuffer.view.StrideInBytes = sizeof(TerrainVertex);

    // Map buffer and fill in vertex data.
    TerrainVertex* vertexData = nullptr;
    m_vertexBuffer.intermediateBuffer->Map(0, nullptr, (void**)&vertexData);
    Assert(vertexData);

    for (int z = 0; z <= CellsPerTileZ; ++z)
    {
        for (int x = 0; x <= CellsPerTileX; ++x)
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

void Terrain::BuildHeightmap(Renderer& renderer, int level)
{
    ClipmapLevel& tile = m_clipmapLevels[level];
    int offset = (HeightmapSize << level) / 2;

    // Generate data.
    tile.heightmap.clear();
    tile.heightmap.reserve(math::Square(HeightmapSize));
    for (int z = 0; z < HeightmapSize; ++z)
    {
        int globalZ = (z << level) - offset;
        for (int x = 0; x < HeightmapSize; ++x)
        {
            int globalX = (x << level) - offset;
            tile.heightmap.push_back(GenerateHeight(globalX, globalZ));
        }
    }

    // Update heightmap texture.
    ID3D12GraphicsCommandList& commandList = renderer.GetCopyCommandList();
    D3D12_SUBRESOURCE_DATA data;
    data.pData = tile.heightmap.data();
    data.RowPitch = HeightmapSize * sizeof(float);
    data.SlicePitch = data.RowPitch * HeightmapSize;
    ::UpdateSubresources<1>(&commandList, tile.texture.Get(), tile.intermediateBuffer.Get(), 0, 0, 1, &data);
}

void Terrain::BuildWater(Renderer& renderer)
{
    const float HalfGridSizeX = 0.5f * CellSize * (float)(CellsPerTileX);// * NumTilesX);
    const float HalfGridSizeZ = 0.5f * CellSize * (float)(CellsPerTileZ);// * NumTilesZ);
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

float Terrain::GenerateHeight(int globalX, int globalZ)
{
    float height = 1.5f;

    // May be better just to use one of the built in stb_perlin functions
    // to generate multiple octaves, but this is quite nice for now.
    for (int i = 0; i < (int)std::size(m_noiseOctaves); ++i)
    {
        auto [frequency, amplitude] = m_noiseOctaves[i];
        height += amplitude * stb_perlin_noise3_seed((float)globalX * frequency, 0.f, (float)globalZ * frequency, 0, 0, 0, m_seed + i);
    }

    return height;
}

Vec2f Terrain::ToVertexPos(int globalX, int globalZ)
{
    return Vec2f(
        CellSize * ((float)globalX - 0.5f * (float)(CellsPerTileX)),// * NumTilesX)),
        CellSize * ((float)globalZ - 0.5f * (float)(CellsPerTileZ)));// * NumTilesZ)));
}

}

#pragma once

namespace gaia
{
namespace TerrainConstants
{

static constexpr int VertexGridDimension = 256;                                     // Number of vertices in each dimension of the vertex grid.
static constexpr int VertexBufferLength = math::Square(VertexGridDimension);        // Number of vertices in the vertex buffer.
static constexpr int IndexBufferLength = 4 * math::Square(VertexGridDimension - 1); // Number of indices in the index buffer.
static constexpr int HeightmapDimension = 256;                                      // Dimension of each heightmap texture.
static constexpr Vec2i HeightmapSize(HeightmapDimension, HeightmapDimension);       // 2D texture size helper.
static constexpr float TexelSize = 0.05f;                                           // World size of a texel at clip level 0.
static constexpr float VertexPatchSize = 0.05f * 64.f;                              // World size of a vertex patch.
static constexpr DXGI_FORMAT HeightmapTexFormat = DXGI_FORMAT_R32_FLOAT;            // Texture format for the height map.
static constexpr DXGI_FORMAT NormalMapTexFormat = DXGI_FORMAT_R8G8B8A8_SNORM;       // Texture format for the normal map.
                                                                                    // TODO: Uint? Unorm? Possible to ditch alpha channel?
static constexpr int TileDimension = 64;                                            // Dimension of each tile/chunk in m_tileCaches.
static_assert(VertexBufferLength <= (1 << 16), "Index format too small");
static_assert(HeightmapDimension * sizeof(float) == GetTexturePitchBytes(HeightmapDimension, sizeof(float)), "Heightmap size does not meet DX12 alignment requirements");

} // namespace TerrainConstants
} // namespace gaia

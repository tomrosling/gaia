#include "TerrainErosion.hpp"
#include "DebugDraw.hpp"
#include "TerrainConstants.hpp"

namespace gaia
{

using namespace TerrainConstants;

void TerrainErosion::Simulate(HeightmapData& heightmap, int dimension)
{
    m_heightmap = &heightmap;
    m_dimension = dimension;

    for (int i = 0; i < 64; ++i)
        CreateRainParticle();
}

void TerrainErosion::CreateRainParticle()
{
    Vec2i pos(rand() % m_dimension, rand() % m_dimension);
    m_particles.push_back(Particle{ pos, 0.f });
}

void TerrainErosion::StepParticles()
{
    for (Particle& p : m_particles)
    {
        // Gather or deposit sediment at current position
        //const float DepositPerStep = 0.01f;
        const float GatherPerStep = 0.00005f;
        //const float ParticleCapacity = 0.5f; // TODO: should seek towards this rather than clamp

        //float gather = math::min(ParticleCapacity - p.sedimentAmount, GatherPerStep);
        float prevHeight = FLT_MAX;//HeightAt(p.position);
        HeightAt(p.position) -= GatherPerStep;
        p.sedimentAmount += GatherPerStep;

        // Move in direction on steepest gradient
        float minAdjacentHeight = FLT_MAX;
        Vec2i destination(INT_MAX, INT_MAX);
        auto tryCandidatePosition = [&](Vec2i candidate)
        {
            float candidateHeight = HeightAt(candidate);
            if (candidateHeight < prevHeight && candidateHeight < minAdjacentHeight)
            {
                destination = candidate;
                minAdjacentHeight = candidateHeight;
            }
        };

        if (p.position.x > 0)
        {
            tryCandidatePosition(p.position - Vec2i(1, 0));
        }
        if (p.position.x < m_dimension - 1)
        {
            tryCandidatePosition(p.position + Vec2i(1, 0));
        }
        if (p.position.y > 0)
        {
            tryCandidatePosition(p.position - Vec2i(0, 1));
        }
        if (p.position.y < m_dimension - 1)
        {
            tryCandidatePosition(p.position + Vec2i(0, 1));
        }

        if (destination != Vec2i(INT_MAX, INT_MAX))
        {
            p.position = destination;   
        }
        else
        {
            // Nowhere to go...
            // Replace with a fresh particle
            p.position = Vec2i(rand() % m_dimension, rand() % m_dimension);
            p.sedimentAmount = 0.f;
        }
    }
}

void TerrainErosion::DebugRender()
{
    for (const Particle& p : m_particles)
    {
        Vec3f worldPos = Vec3f(p.position.x * TexelSize, (*m_heightmap)[HeightmapIndex(p.position)], p.position.y * TexelSize);
        DebugDraw::Instance().Point(worldPos, 2.f, Vec4u8(0x00, 0x80, 0xff, 0xff));
    }
}

float& TerrainErosion::HeightAt(Vec2i position)
{
    return (*m_heightmap)[HeightmapIndex(position)];
}

int TerrainErosion::HeightmapIndex(Vec2i position) const
{
    Assert(0 <= position.x && position.x < m_dimension);
    Assert(0 <= position.y && position.y < m_dimension);
    return position.y * m_dimension + position.x;
}

}

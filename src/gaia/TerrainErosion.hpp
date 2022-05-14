#pragma once

namespace gaia
{

class TerrainErosion
{
    using HeightmapData = std::vector<float>;

public:
    //using ParticleID = int;
    struct Particle
    {
        Vec2i position;
        float sedimentAmount;
    };

    void Simulate(HeightmapData& heightmap, int dimension);
    void CreateRainParticle();
    void StepParticles();
    void DebugRender();

private:
    float& HeightAt(Vec2i position);
    int HeightmapIndex(Vec2i position) const;

    HeightmapData* m_heightmap = nullptr; // TODO: storing this is a little dodgy
    int m_dimension = 0; // Heightmap dimension
    std::vector<Particle> m_particles;
};

}

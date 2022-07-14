#pragma once

namespace gaia
{

//
// A finite ray/line segment.
//
class Rayf
{
public:
    Rayf(const Vec3f& start, const Vec3f& normalisedDir, float length)
        : m_start(start)
        , m_dir(normalisedDir)
        , m_length(length)
    {
    }

    Rayf(const Vec3f& start, const Vec3f& dirAndLength)
        : m_start(start)
    {
        m_length = dirAndLength.length();
        Assert(m_length >= Epsilonf);
        m_dir = dirAndLength / m_length;
    }

    Vec3f m_start = Vec3fZero;
    Vec3f m_dir = Vec3fZero; // Normalised direction
    float m_length = 0.f;
};

} // namespace gaia

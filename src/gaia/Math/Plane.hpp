#pragma once
#include "Ray.hpp"

namespace gaia
{

//
// A plane defined by the equation: dot(p, m_normal) == m_depth
//
class Planef
{
public:
    Planef(const Vec3f& normal, float depth)
        : m_normal(normal)
        , m_depth(depth)
    {
    }

    float RayIntersect(const Rayf& ray)
    {
        float rayDirDotNormal = math::dot(ray.m_dir, m_normal);
        if (fabsf(rayDirDotNormal) < Epsilonf)
            return -1.f;

        return (m_depth - math::dot(ray.m_start, m_normal)) / (ray.m_length * rayDirDotNormal);
    }

    Vec3f m_normal = Vec3fZero;
    float m_depth = 0.f;
};

} // namespace gaia

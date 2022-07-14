#pragma once

namespace gaia
{

class AABB3f
{
public:
    bool IsValid() const
    {
        return math::all(math::greaterThanEqual(m_max, m_min));
    }

    void SetFromPoints(int numPoints, Vec3f* points)
    {
        Assert(numPoints > 0);
        Vec3f minp = points[0];
        Vec3f maxp = points[0];
        for (int i = 1; i < numPoints; ++i)
        {
            minp = math::min(minp, points[i]);
            maxp = math::max(maxp, points[i]);
        }

        m_min = minp;
        m_max = maxp;
    }

    AABB3f AffineTransformed(const Mat4f& mat)
    {
        math::AssertMat4fIsAffine(mat);
        Vec3f translation = math::Mat4fGetTranslation(mat);
        AABB3f ret = { translation, translation };

        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                float a = mat[i][j] * m_min[i];
                float b = mat[i][j] * m_max[i];
                if (a < b)
                {
                    ret.m_min[j] += a;
                    ret.m_max[j] += b;
                }
                else
                {
                    ret.m_min[j] += b;
                    ret.m_max[j] += a;
                }
            }
        }

        return ret;
    }

    AABB3f Transformed(const Mat4f& mat)
    {
        Vec3f v[8];
        v[0] = Vec3f(m_min.x, m_min.y, m_min.z);
        v[1] = Vec3f(m_max.x, m_min.y, m_min.z);
        v[2] = Vec3f(m_max.x, m_max.y, m_min.z);
        v[3] = Vec3f(m_min.x, m_max.y, m_min.z);
        v[4] = Vec3f(m_min.x, m_min.y, m_max.z);
        v[5] = Vec3f(m_max.x, m_min.y, m_max.z);
        v[6] = Vec3f(m_max.x, m_max.y, m_max.z);
        v[7] = Vec3f(m_min.x, m_max.y, m_max.z);

        for (int i = 0; i < 8; i++)
            v[i] = math::Mat4fTransformVec3f(mat, v[i]);

        AABB3f ret;
        ret.SetFromPoints(8, v);
        return ret;
    }

    Vec3f m_min = Vec3fZero;
    Vec3f m_max = Vec3fZero;
};

static AABB3f AABB3fInvalid { Vec3f(FLT_MAX, FLT_MAX, FLT_MAX), Vec3f(-FLT_MAX, -FLT_MAX, -FLT_MAX) };

} // namespace gaia

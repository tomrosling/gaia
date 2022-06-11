#pragma once

namespace gaia
{

class AABB3f
{
public:
    AABB3f Transformed(const Mat4f& mat)
    {
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

    Vec3f m_min;
    Vec3f m_max;
};

} // namespace gaia

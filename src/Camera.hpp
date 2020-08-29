#pragma once

namespace gaia
{

class Camera
{
public:
    Mat4f Update(float dt);

private:
    // State
    Vec3f m_pos{ 0.f, 1.f, -5.f };
    float m_rotx = 0.f;
    float m_rotY = 0.f;

    // Config
    float m_linSpeed = 3.f;
    float m_rotSpeed = 3.f;
};

}

#pragma once

namespace gaia
{

class Input;

class Camera
{
public:
    Mat4f Update(const Input& input, float dt);

private:
    // State
    Vec3f m_pos{ 0.f, 1.f, -5.f };
    Vec2f m_rot = Vec2fZero;

    // Config
    float m_linSpeed = 3.f;
    Vec2f m_rotSpeed{ 0.5f, 0.4f };
};

}

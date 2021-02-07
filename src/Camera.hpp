#pragma once

namespace gaia
{

class Input;

class Camera
{
public:
    Mat4f Update(const Input& input, float dt);
    Mat4f GetMatrix() const;

private:
    // State
    Vec3f m_pos{ 0.f, 3.f, 10.f };
    Vec2f m_rot = Vec2fZero;

    // Config
    float m_linSpeed = 3.f;
    Vec2f m_rotSpeed{ 0.0035f, 0.003f }; // Radians per pixel of mouse movement.
};

}

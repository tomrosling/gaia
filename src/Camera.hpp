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
    Vec3f m_pos{ 0.f, 15.f, 0.f };
    Vec2f m_rot{ -0.707, 0.f };

    // Config
    float m_linSpeed = 8.f;
    Vec2f m_rotSpeed{ 0.0035f, 0.003f }; // Radians per pixel of mouse movement.
};

}

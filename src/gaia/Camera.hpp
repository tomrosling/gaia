#pragma once

namespace gaia
{

class Input;

class Camera
{
public:
    void SetTransform(const Vec3f& pos, Vec2f rot) { m_pos = pos; m_rot = rot; }
    Mat4f Update(const Input& input, float dt);
    Mat4f GetMatrix() const;

private:
    // State
    Vec3f m_pos{ 0.f, 40.f, 0.f };
    Vec2f m_rot{ -0.5f, 1.25f * Pif };

    // Config
    float m_linSpeed = 8.f;
    Vec2f m_rotSpeed{ 0.0035f, 0.003f }; // Radians per pixel of mouse movement.
};

}

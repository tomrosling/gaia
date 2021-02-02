#include "Camera.hpp"
#include "Input.hpp"

namespace gaia
{

Mat4f Camera::Update(const Input& input, float dt)
{
    if (input.IsMouseButtonDown(MouseButton::Right))
    {
        Vec2f mouseDelta = (Vec2f)input.GetMouseDelta();
        Vec2f swizzle(-mouseDelta.y * m_rotSpeed.x, -mouseDelta.x * m_rotSpeed.y);
        m_rot += dt * swizzle;
        m_rot.x = std::clamp(m_rot.x, -0.5f * Pif, 0.5f * Pif);
    }

    Vec3f translation(0.f, 0.f, 0.f);
    if (input.IsCharKeyDown('A'))
        translation.x -= m_linSpeed * dt;
    if (input.IsCharKeyDown('D'))
        translation.x += m_linSpeed * dt;
    if (input.IsCharKeyDown('Q'))
        translation.y -= m_linSpeed * dt;
    if (input.IsCharKeyDown('E'))
        translation.y += m_linSpeed * dt;
    if (input.IsCharKeyDown('S'))
        translation.z += m_linSpeed * dt;
    if (input.IsCharKeyDown('W'))
        translation.z -= m_linSpeed * dt;

    if (input.IsSpecialKeyDown(SpecialKey::Shift))
        translation *= 5.f;

    Mat3f rotMat = math::Mat3fMakeRotationY(m_rot.y) * math::Mat3fMakeRotationX(m_rot.x);
    m_pos += rotMat * translation;
    return math::Mat4fCompose(rotMat, m_pos);
}

Mat4f Camera::GetMatrix() const
{
    Mat3f rotMat = math::Mat3fMakeRotationY(m_rot.y) * math::Mat3fMakeRotationX(m_rot.x);
    return math::Mat4fCompose(rotMat, m_pos);
}

}

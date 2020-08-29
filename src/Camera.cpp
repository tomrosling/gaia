#include "Camera.hpp"

namespace gaia
{

Mat4f Camera::Update(float dt)
{
    // TODO: Separate input system
    auto keyDown = [](int key)
    {
        return (::GetKeyState(key) & 0x8000) != 0;
    };
    
    if (keyDown(VK_UP))
        m_rotx -= m_rotSpeed * dt;
    if (keyDown(VK_DOWN))
        m_rotx += m_rotSpeed * dt;
    if (keyDown(VK_LEFT))
        m_rotY -= m_rotSpeed * dt;
    if (keyDown(VK_RIGHT))
        m_rotY += m_rotSpeed * dt;

    Vec3f translation(0.f, 0.f, 0.f);
    if (keyDown('A'))
        translation.x -= m_linSpeed * dt;
    if (keyDown('D'))
        translation.x += m_linSpeed * dt;
    if (keyDown('Q'))
        translation.y -= m_linSpeed * dt;
    if (keyDown('E'))
        translation.y += m_linSpeed * dt;
    if (keyDown('S'))
        translation.z -= m_linSpeed * dt;
    if (keyDown('W'))
        translation.z += m_linSpeed * dt;

    Mat3f rotMat = math::Mat3fMakeRotationY(m_rotY) * math::Mat3fMakeRotationX(m_rotx);
    m_pos += rotMat * translation;
    return math::Mat4fCompose(rotMat, m_pos);
}

}

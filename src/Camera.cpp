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

    // TODO: These functions are unintuitive.
    // e.g. translate() pre-multiplies a translation matrix, onto the first param,
    // rather than just post-applying the translation.
    // Write our own, and also some constants for identity and unit vectors.
    Mat4f rotX = math::rotate(math::identity<Mat4f>(), m_rotx, Vec3f(1.f, 0.f, 0.f));
    Mat4f rotY = math::rotate(math::identity<Mat4f>(), m_rotY, Vec3f(0.f, 1.f, 0.f));
    Mat4f rotMat = rotY * rotX;

    m_pos += Vec3f(rotMat * Vec4f(translation, 1.f));
    Mat4f trans = math::translate(Mat4f(1.f), m_pos);
    rotMat = trans * rotMat;
    return rotMat;
}

}

#include "Camera.hpp"

// TODO: Separate input system
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace gaia
{

using namespace DirectX;

XMMATRIX Camera::Update(float dt)
{
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

    XMVECTORF32 translation = { 0.f, 0.f, 0.f, 1.f };
    if (keyDown('A'))
        translation.f[0] -= m_linSpeed * dt;
    if (keyDown('D'))
        translation.f[0] += m_linSpeed * dt;
    if (keyDown('Q'))
        translation.f[1] -= m_linSpeed * dt;
    if (keyDown('E'))
        translation.f[1] += m_linSpeed * dt;
    if (keyDown('S'))
        translation.f[2] -= m_linSpeed * dt;
    if (keyDown('W'))
        translation.f[2] += m_linSpeed * dt;

    XMMATRIX rotMat = XMMatrixRotationX(m_rotx) * XMMatrixRotationY(m_rotY);
    m_pos.v += XMVector3Transform(translation, rotMat);
    return rotMat * XMMatrixTranslationFromVector(m_pos);
}

}

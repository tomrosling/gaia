#pragma once

namespace gaia
{

class Camera
{
public:
    DirectX::XMMATRIX Update(float dt);

private:
    // State
    DirectX::XMVECTORF32 m_pos = { 0.f, 0.f, -5.f, 1.f };
    float m_rotx = 0.f;
    float m_rotY = 0.f;

    // Config
    float m_linSpeed = 3.f;
    float m_rotSpeed = 3.f;
};

}

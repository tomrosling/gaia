#pragma once
#include "Camera.hpp"
#include "DebugDraw.hpp"
#include "Input.hpp"
#include "Renderer.hpp"
#include "Terrain.hpp"
#include "Timer.hpp"

class GaiaTestbedApp
{
public:
    bool Init(HWND hwnd);
    bool WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    int Run();

private:
    void Update(float dt);
    void Render();

    gaia::Renderer m_renderer;
    gaia::Camera m_camera;
    gaia::Terrain m_terrain;
    gaia::Input m_input;
    gaia::Timer m_timer;
    bool m_trackingMouseLeave = false;
};

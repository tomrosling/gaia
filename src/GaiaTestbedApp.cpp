#include "GaiaTestbedApp.hpp"

using namespace gaia;

bool GaiaTestbedApp::Init(HWND hwnd)
{
    if (FAILED(::CoInitialize(nullptr)))
        return false;

    if (!m_renderer.Create(hwnd))
        return false;

    if (!m_terrain.LoadCompiledShaders(m_renderer))
        return false;

    // By this point, assume we have enough driver support to go without further error checks...
    DebugDraw::Instance().Init(m_renderer);
    m_terrain.Build(m_renderer);

    return true;
}

bool GaiaTestbedApp::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return true;

    case WM_GETMINMAXINFO:
    {
        // Define minimum window size.
        MINMAXINFO* minMaxInfo = (MINMAXINFO*)lParam;
        minMaxInfo->ptMinTrackSize.x = 128;
        minMaxInfo->ptMinTrackSize.y = 128;
        return true;
    }

    case WM_SIZE:
    {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        if (!m_renderer.ResizeViewport(width, height))
        {
            DebugOut("Failed to resize window; quitting.\n");
            ::PostQuitMessage(1);
        }
        return true;
    }

    case WM_MOUSELEAVE:
        m_input.LoseFocus();
        m_trackingMouseLeave = false;
        return true;

    case WM_MOUSEMOVE:
    {
        if (!m_trackingMouseLeave)
        {
            // Subscribe to WM_MOUSELEAVE.
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = HOVER_DEFAULT;
            ::TrackMouseEvent(&tme);

            m_trackingMouseLeave = true;
        }

        Vec2i pos(LOWORD(lParam), HIWORD(lParam));
        m_input.MouseMove(pos);
        return true;
    }

    case WM_LBUTTONDOWN:
        m_input.SetMouseButtonDown(MouseButton::Left);
        return true;

    case WM_LBUTTONUP:
        m_input.SetMouseButtonUp(MouseButton::Left);
        return true;

    case WM_RBUTTONDOWN:
        m_input.SetMouseButtonDown(MouseButton::Right);
        return true;

    case WM_RBUTTONUP:
        m_input.SetMouseButtonUp(MouseButton::Right);
        return true;

    case WM_MBUTTONDOWN:
        m_input.SetMouseButtonDown(MouseButton::Middle);
        return true;

    case WM_MBUTTONUP:
        m_input.SetMouseButtonUp(MouseButton::Middle);
        return true;

    case WM_KEYDOWN:
        if ('A' <= wParam && wParam <= 'Z')
        {
            m_input.SetCharKeyDown((char)wParam);
        }

        if (wParam == VK_SHIFT)
        {
            m_input.SetSpecialKeyDown(SpecialKey::Shift);
        }
        return true;

    case WM_KEYUP:
        if ('A' <= wParam && wParam <= 'Z')
        {
            m_input.SetCharKeyUp((char)wParam);
        }

        if (wParam == VK_SHIFT)
        {
            m_input.SetSpecialKeyUp(SpecialKey::Shift);
        }

        if (wParam == VK_ESCAPE)
        {
            ::PostQuitMessage(0);
        }

        if (wParam == VK_F5)
        {
            m_terrain.HotloadShaders(m_renderer);
        }

        if (wParam == VK_F6)
        {
            m_renderer.WaitCurrentFrame();
            m_terrain.Build(m_renderer);
        }

        if (wParam == 'T')
        {
            m_terrainEditEnabled ^= 1;
        }

        return true;
    }

    return false;
}

int GaiaTestbedApp::Run()
{
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }

        float dt = m_timer.GetSecondsAndReset();
        dt = std::min(dt, 0.1f);
        Update(dt);
        Render();
    }

    // Wait for GPU operations to finish before shutting down.
    m_renderer.WaitCurrentFrame();

    // Return the WM_QUIT return code.
    return (int)msg.wParam;
}

void GaiaTestbedApp::Update(float dt)
{
    int currentBuffer = m_renderer.GetCurrentBuffer();
    float highlightRadius = 0.f;
    Vec2f highlightPos = Vec2fZero;

    if (m_terrainEditEnabled)
    {
        // Do mouse picking (before updating the camera matrix).
        Vec2i mousePos = m_input.GetMousePos();
        float depth = m_renderer.ReadDepth((int)mousePos.x, (int)mousePos.y);
        constexpr float modifyRadius = 3.f;
        if (depth < 1.f)
        {
            Mat4f oldCamMat = m_camera.GetMatrix();
            Vec3f pickPointViewSpace = m_renderer.Unproject(Vec3f((Vec2f)mousePos, depth));
            Vec3f pickPointWorldSpace = math::Mat4fTransformVec3f(oldCamMat, pickPointViewSpace);

            if (m_input.IsMouseButtonDown(MouseButton::Left))
            {
                m_terrain.RaiseAreaRounded(m_renderer, Vec2f(pickPointWorldSpace.x, pickPointWorldSpace.z), modifyRadius, 0.002f);
            }
            else if (m_input.IsMouseButtonDown(MouseButton::Right))
            {
                m_terrain.RaiseAreaRounded(m_renderer, Vec2f(pickPointWorldSpace.x, pickPointWorldSpace.z), modifyRadius, -0.002f);
            }

            highlightRadius = modifyRadius;
            highlightPos = Vec2f(pickPointWorldSpace.x, pickPointWorldSpace.z);
        }
    }

    m_terrain.SetHighlightRadius(highlightRadius, currentBuffer);
    m_terrain.SetHighlightPos(highlightPos, currentBuffer);
    
    // Update the view matrix
    Mat4f camMat = m_camera.Update(m_input, dt);
    Mat4f viewMat = math::affineInverse(camMat);
    m_renderer.SetViewMatrix(viewMat);

    m_input.EndFrame();
}

void GaiaTestbedApp::Render()
{
    m_renderer.BeginFrame();
    m_terrain.Render(m_renderer);
    DebugDraw::Instance().Render(m_renderer);
    m_renderer.EndFrame();
}

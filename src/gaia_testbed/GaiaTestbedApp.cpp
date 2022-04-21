#include "GaiaTestbedApp.hpp"
#include <imgui.h>

using namespace gaia;

bool GaiaTestbedApp::Init(HWND hwnd)
{
    m_hwnd = hwnd;

    if (FAILED(::CoInitialize(nullptr)))
        return false;

    if (!m_renderer.Create(hwnd))
        return false;

    if (!m_terrain.Init(m_renderer))
        return false;

    if (!m_skybox.Init(m_renderer))
        return false;

    // By this point, assume we have enough driver support to go without further error checks...
    DebugDraw::Instance().Init(m_renderer);
    m_terrain.Build(m_renderer);

    return true;
}

LRESULT GaiaTestbedApp::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // Process imgui first.
    extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam) != 0)
        return 1;

    switch (uMsg)
    {
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;

    case WM_GETMINMAXINFO:
    {
        // Define minimum window size.
        MINMAXINFO* minMaxInfo = (MINMAXINFO*)lParam;
        minMaxInfo->ptMinTrackSize.x = 128;
        minMaxInfo->ptMinTrackSize.y = 128;
        return 0;
    }

    case WM_SIZE:
    {
        m_windowSize = Vec2i(LOWORD(lParam), HIWORD(lParam));
        if (!m_renderer.ResizeViewport(m_windowSize.x, m_windowSize.y))
        {
            DebugOut("Failed to resize window; quitting.\n");
            ::PostQuitMessage(1);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_input.LoseFocus();
        if (m_input.DisableCursorLock())
        {
            ::ShowCursor(TRUE);
        }
        m_trackingMouseLeave = false;
        return 0;

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

        if (ImGui::GetIO().WantCaptureMouse)
            return 0;

        // ImGui might call SetCapture() so we have to clamp this.
        Vec2i pos(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        pos = math::clamp(pos, Vec2iZero, m_windowSize - Vec2i(1, 1));
        m_input.MouseMove(pos);
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (ImGui::GetIO().WantCaptureMouse)
            return 0;

        m_input.SetMouseButtonDown(MouseButton::Left);
        return 0;

    case WM_LBUTTONUP:
        if (ImGui::GetIO().WantCaptureMouse)
            return 0;

        m_input.SetMouseButtonUp(MouseButton::Left);
        return 0;

    case WM_RBUTTONDOWN:
        if (ImGui::GetIO().WantCaptureMouse)
            return 0;

        m_input.SetMouseButtonDown(MouseButton::Right);

        {
            POINT point;
            ::GetCursorPos(&point);
            ::ScreenToClient(hwnd, &point);
            m_input.EnableCursorLock(Vec2i(point.x, point.y));
            ::ShowCursor(FALSE);
        }
        return 0;

    case WM_RBUTTONUP:
        if (ImGui::GetIO().WantCaptureMouse)
            return 0;

        m_input.SetMouseButtonUp(MouseButton::Right);

        if (m_input.DisableCursorLock())
        {
            ::ShowCursor(TRUE);
        }
        return 0;

    case WM_MBUTTONDOWN:
        if (ImGui::GetIO().WantCaptureMouse)
            return 0;

        m_input.SetMouseButtonDown(MouseButton::Middle);
        return 0;

    case WM_MBUTTONUP:
        if (ImGui::GetIO().WantCaptureMouse)
            return 0;

        m_input.SetMouseButtonUp(MouseButton::Middle);
        return 0;

    case WM_KEYDOWN:
        if (ImGui::GetIO().WantCaptureKeyboard)
            return 0;

        // Ignore key repeats.
        if (lParam & (1 << 30))
            return 0;

        if ('A' <= wParam && wParam <= 'Z')
        {
            m_input.SetCharKeyDown((char)wParam);
        }

        switch (wParam)
        {
        case VK_SHIFT:
            m_input.SetSpecialKeyDown(SpecialKey::Shift);
            break;
        case VK_CONTROL:
            m_input.SetSpecialKeyDown(SpecialKey::Ctrl);
            break;
        default:
            break;
        }
        return 0;

    case WM_KEYUP:
        if (ImGui::GetIO().WantCaptureKeyboard)
            return 0;

        if ('A' <= wParam && wParam <= 'Z')
        {
            m_input.SetCharKeyUp((char)wParam);
        }

        switch (wParam)
        {
        case VK_SHIFT:
            m_input.SetSpecialKeyUp(SpecialKey::Shift);
            break;
        case VK_CONTROL:
            m_input.SetSpecialKeyUp(SpecialKey::Ctrl);
            break;
        case VK_ESCAPE:
            ::PostQuitMessage(0);
            break;
        case 'T':
            m_terrainEditEnabled ^= 1;
            break;
        default:
            break;
        }

        return 0;
    }

    return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int GaiaTestbedApp::Run()
{
    MSG msg = {};
    while (true)
    {
        while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
            {
                // Wait for GPU operations to finish before shutting down.
                m_renderer.WaitCurrentFrame();

                // Return the WM_QUIT return code.
                return (int)msg.wParam;
            }
        }

        float dt = m_timer.GetSecondsAndReset();
        dt = std::min(dt, 0.1f);

        m_renderer.BeginImguiFrame();
        Update(dt);
        Render();
    }

    Assert(!"Unreachable");
    return -1;
}

void GaiaTestbedApp::Update(float dt)
{
    Vec2i cursorLockPos = m_input.GetCursorLockPos();
    if (cursorLockPos.x != -1 && cursorLockPos.y != -1)
    {
        ::ClientToScreen(m_hwnd, (POINT*)&cursorLockPos);
        ::SetCursorPos(cursorLockPos.x, cursorLockPos.y);
    }

    m_terrain.Imgui(m_renderer);

    int currentBuffer = m_renderer.GetCurrentBuffer();
    float highlightRadius = 0.f;
    Vec2f highlightPos = Vec2fZero;

    if (m_terrainEditEnabled && !m_input.IsCursorLocked() && IsMouseInWindow())
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
                float offset = m_input.IsSpecialKeyDown(SpecialKey::Ctrl) ? -0.005f : 0.005f;
                m_terrain.RaiseAreaRounded(m_renderer, Vec2f(pickPointWorldSpace.x, pickPointWorldSpace.z), modifyRadius, offset);
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
    m_skybox.Render(m_renderer);
    DebugDraw::Instance().Render(m_renderer);
    m_renderer.EndFrame();
}

bool GaiaTestbedApp::IsMouseInWindow() const
{
    Vec2i mousePos = m_input.GetMousePos();
    return 0 <= mousePos.x && mousePos.x < m_windowSize.x
        && 0 <= mousePos.y && mousePos.y < m_windowSize.y;
}

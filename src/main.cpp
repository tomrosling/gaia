#include "Renderer.hpp"
#include "Camera.hpp"
#include "Terrain.hpp"
#include "Input.hpp"
#include "DebugDraw.hpp"

using namespace gaia;

Renderer g_renderer;
Camera g_camera;
Terrain g_terrain;
Input g_input;
bool g_trackingMouseLeave = false;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, PSTR pCmdLine, int nCmdShow)
{
    // Register the window class.
    const char CLASS_NAME[] = "GaiaWindow";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = ::LoadCursor(nullptr, IDC_CROSS);
    RegisterClass(&wc);

    // Create the window.
    HWND hwnd = CreateWindowEx(
        0,                     // Optional window styles.
        CLASS_NAME,            // Window class
        "Gaia Engine Testbed", // Window text
        WS_OVERLAPPEDWINDOW,   // Window style

        // Size and position
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,

        nullptr,   // Parent window
        nullptr,   // Menu
        hInstance, // Instance handle
        nullptr    // Additional application data
    );

    if (hwnd == nullptr)
        return 1;

    if (!g_renderer.Create(hwnd))
        return 1;

    if (!g_renderer.LoadCompiledShaders())
        return 1;

    // By this point, assume we have enough driver support to go without further error checks...
    DebugDraw::Instance().Init(g_renderer);
    g_terrain.Build(g_renderer);
    
    ::ShowWindow(hwnd, SW_SHOW);

    // Main loop:
    MSG msg = {};
    while (::GetMessage(&msg, nullptr, 0, 0))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    // Return the WM_QUIT return code.
    return (int)msg.wParam;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
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
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        if (!g_renderer.ResizeViewport(width, height))
        {
            DebugOut("Failed to resize window; quitting.\n");
            ::PostQuitMessage(1);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_input.LoseFocus();
        g_trackingMouseLeave = false;
        return 0;

    case WM_MOUSEMOVE:
    {
        if (!g_trackingMouseLeave)
        {
            // Subscribe to WM_MOUSELEAVE.
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = HOVER_DEFAULT;
            ::TrackMouseEvent(&tme);

            g_trackingMouseLeave = true;
        }

        Vec2i pos(LOWORD(lParam), HIWORD(lParam));
        g_input.MouseMove(pos);
        return 0;
    }

    case WM_KEYDOWN:
        if ('A' <= wParam && wParam <= 'Z')
        {
            g_input.SetCharKeyDown((char)wParam);
        }

        if (wParam == VK_SHIFT)
        {
            g_input.SetSpecialKeyDown(SpecialKey::Shift);
        }
        return 0;

    case WM_KEYUP:
        if ('A' <= wParam && wParam <= 'Z')
        {
            g_input.SetCharKeyUp((char)wParam);
        }

        if (wParam == VK_SHIFT)
        {
            g_input.SetSpecialKeyUp(SpecialKey::Shift);
        }

        if (wParam == VK_ESCAPE)
        {
            ::PostQuitMessage(0);
            return 0;
        }

        if (wParam == VK_F5)
        {
            g_renderer.HotloadShaders();
        }

        if (wParam == VK_F6)
        {
            g_renderer.WaitCurrentFrame();
            g_terrain.Build(g_renderer);
        }
        return 0;

    case WM_PAINT:
    {
        // Update the view matrix
        // TODO: Actually measure time (and put this all in a better place)!
        Mat4f camMat = g_camera.Update(g_input, 0.016f);
        Mat4f viewMat = math::affineInverse(camMat);
        g_renderer.SetViewMatrix(viewMat);

        Vec3f mousePosFarClip = g_renderer.Unproject(Vec3f((Vec2f)g_input.GetMousePos(), 1.f));
        Vec3f rayStart(camMat[3]);
        Vec3f rayEnd = math::Mat4fTransformVec3f(camMat, mousePosFarClip);
        float t = g_terrain.Raycast(rayStart, rayEnd);
        if (t >= 0.f)
        {
            Vec3f hit = math::Lerp(rayStart, rayEnd, t);
            DebugDraw::Instance().Point(hit, 0.5f, Vec4u8(0xff, 0xff, 0xff, 0xff));

            if (g_input.IsCharKeyDown('R'))
            {
                g_terrain.RaiseAreaRounded(g_renderer, Vec2f(hit.x, hit.z), 3.f, 0.002f);
            }

            if (g_input.IsCharKeyDown('L'))
            {
                g_terrain.RaiseAreaRounded(g_renderer, Vec2f(hit.x, hit.z), 3.f, -0.002f);
            }
        }

        g_renderer.BeginFrame();
        g_terrain.Render(g_renderer);
        DebugDraw::Instance().Render(g_renderer);
        g_renderer.EndFrame();

        g_input.EndFrame();
        return 0;
    }
    }

    return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#include "Renderer.hpp"
#include "Camera.hpp"
#include "Terrain.hpp"

gaia::Renderer g_renderer;
gaia::Camera g_camera;
std::unique_ptr<gaia::Terrain> g_terrain;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, PSTR pCmdLine, int nCmdShow)
{
    // Register the window class.
    const char CLASS_NAME[] = "GaiaWindow";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
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

    if (!g_renderer.CreateDefaultPipelineState())
        return 1;

    // By this point, assume we have enough driver support to go without further error checks...
    g_renderer.CreateHelloTriangle();
    g_terrain = std::make_unique<gaia::Terrain>(g_renderer);
    
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

    case WM_KEYUP:
        if (wParam == VK_ESCAPE)
        {
            ::PostQuitMessage(0);
            return 0;
        }
        break;

    case WM_PAINT:
    {
        g_renderer.BeginFrame();

        // Update the view matrix
        DirectX::XMMATRIX camMat = g_camera.Update(0.016f);           // TODO: Actually measure time!
        DirectX::XMMATRIX viewMat = XMMatrixInverse(nullptr, camMat); // TODO: Fast affine inverse!
        g_renderer.SetViewMatrix(viewMat);

        g_terrain->Render(g_renderer);
        g_renderer.RenderHelloTriangle();

        g_renderer.EndFrame();
        return 0;
    }
    }

    return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
}

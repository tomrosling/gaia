#include "GaiaTestbedApp.hpp"

static GaiaTestbedApp g_app;

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

    if (!g_app.Init(hwnd))
        return 1;

    ::ShowWindow(hwnd, SW_SHOW);

    // Main loop.
    int ret = g_app.Run();

    return ret;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (g_app.WindowProc(hwnd, uMsg, wParam, lParam))
        return 0;

    return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
}

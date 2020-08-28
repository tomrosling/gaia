#include <memory>
#include <cstdlib>
#include <cassert>
#include <climits>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl/client.h>

#include <DirectXMath.h>
#include <d3d12.h>
#include <d3dx12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

namespace gaia
{

using Microsoft::WRL::ComPtr;

inline void DebugOut(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int size = vsnprintf(nullptr, 0, fmt, args);
    char* buf = (char*)alloca(size);
    vsnprintf(buf, size, fmt, args);
    OutputDebugStringA(buf);
    va_end(args);
}

}
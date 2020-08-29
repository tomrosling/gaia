#include <memory>
#include <cstdlib>
#include <cassert>
#include <climits>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl/client.h>

#undef max
#undef min

#include <DirectXMath.h>
#include <d3d12.h>
#include <d3dx12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

namespace gaia
{

using Microsoft::WRL::ComPtr;

using Vec2f = glm::vec2;
using Vec3f = glm::vec3;
using Vec4f = glm::vec4;
using Mat3f = glm::mat3;
using Mat4f = glm::mat4;
using Quatf = glm::quat;

// Alias namespace used for global functions (dot, cross, etc).
namespace math = glm;

inline void DebugOut(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int size = vsnprintf(nullptr, 0, fmt, args);
    char* buf = (char*)alloca(size);
    vsnprintf(buf, size, fmt, args);
    ::OutputDebugStringA(buf);
    va_end(args);
}

}

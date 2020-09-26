#pragma once

#ifdef _DEBUG
#define Assert(expr)                                                                      \
    do                                                                                    \
    {                                                                                     \
        if (!(expr))                                                                      \
        {                                                                                 \
            DebugOut("Assertion failed in %s, line %d: %s\n", __FILE__, __LINE__, #expr); \
            _CrtDbgBreak();                                                               \
        }                                                                                 \
    } while (0)
#else
#define Assert(expr)
#endif

namespace gaia
{

using uchar = unsigned char;
using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using int64 = int64_t;
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using float32 = float;
using float64 = double;

using Microsoft::WRL::ComPtr;

inline void DebugOut(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int size = 1 + vsnprintf(nullptr, 0, fmt, args);
    char* buf = (char*)alloca(size);
    vsnprintf(buf, size, fmt, args);
    ::OutputDebugStringA(buf);
    va_end(args);
}

// TODO: Maybe encapsulate the double constant buffer to avoid other code having to deal with swap chains?
static constexpr int BackbufferCount = 2;

}

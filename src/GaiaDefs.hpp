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

}

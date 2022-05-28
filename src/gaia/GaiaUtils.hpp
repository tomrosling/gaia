#pragma once
#include <DirectXTex/DirectXTex.h>

namespace gaia
{

inline const wchar_t* GetFileExtension(const wchar_t* filepath)
{
    if (const wchar_t* dot = std::wcsrchr(filepath, L'.'))
    {
        return dot + 1;
    }

    return nullptr;
}

inline int GetFormatSize(DXGI_FORMAT format)
{
    return (int)DirectX::BitsPerPixel(format) >> 3;
}

constexpr inline int GetTexturePitchBytes(int width, int bytesPerTexel)
{
    return math::RoundUpPow2(width * bytesPerTexel, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
}

}

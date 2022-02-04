#pragma once

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

constexpr inline int GetTexturePitchBytes(int width, int bytesPerTexel)
{
    return math::RoundUpPow2(width * bytesPerTexel, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
}

}

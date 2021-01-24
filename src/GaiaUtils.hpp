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

}

#include "File.hpp"

namespace gaia
{

File::~File()
{
    Close();
}

bool File::Open(const char* filename, EFileOpenMode mode)
{
    Assert(!m_handle);
    m_handle = fopen(filename, OpenModeToString(mode));
    return m_handle != nullptr;
}

void File::Close()
{
    if (m_handle)
    {
        fclose(m_handle);
        m_handle = nullptr;
    }
}

int File::GetLength()
{
    Assert(m_handle);
    Assert(ftell(m_handle) == 0);
    fseek(m_handle, 0, SEEK_END);
    int length = ftell(m_handle);
    rewind(m_handle);
    return length;
}

void File::Read(void* outData, int numBytes)
{
    Assert(m_handle);
    fread(outData, 1, numBytes, m_handle);
}

const char* File::OpenModeToString(EFileOpenMode mode)
{
    switch (mode)
    {
    case EFileOpenMode::Read:
        return "rb";
    case EFileOpenMode::Write:
        return "wb";
    default:
        Assert(false);
    }

    return nullptr;
}

} // namespace gaia

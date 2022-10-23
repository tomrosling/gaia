#pragma once

namespace gaia
{

enum class EFileOpenMode
{
    Read,
    Write,
};

class File
{
public:
    ~File();

    bool Open(const char* filename, EFileOpenMode mode);
    void Close();
    int GetLength();
    void Read(void* outData, int numBytes);

    static const char* OpenModeToString(EFileOpenMode mode);

private:
    FILE* m_handle = nullptr;
};

} // namespace gaia

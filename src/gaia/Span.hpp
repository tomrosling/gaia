#pragma once

namespace gaia
{

/*
 * std::span equivalent. Represents a view into a contiguous array, with a size.
 */
template<typename T>
class Span
{
public:
    Span() = default;

    Span(T* data, size_t size)
        : m_data(data)
        , m_size(size)
    {
    }

    // Static array constructor.
    template <size_t ArraySize>
    Span(T (&staticArrayData)[ArraySize])
        : Span<T>(staticArrayData, ArraySize)
    {
    }

    T* Data() const { return m_data; }
    size_t Size() const { return m_size; }

    T& operator[](size_t i) const
    {
        Assert(i < this->m_size);
        return this->m_data[i];
    }

private:
    T* m_data = nullptr;
    size_t m_size = 0;
};


/*
 * Helper functions for casting static arrays to Span<uchar>.
 */
template <typename InType, size_t ArraySize>
Span<uchar> MakeUCharSpan(InType(&staticArrayData)[ArraySize])
{
    return Span<uchar>(reinterpret_cast<uchar*>(staticArrayData), ArraySize * sizeof(InType));
}

template <typename InType, size_t ArraySize>
Span<const uchar> MakeConstUCharSpan(InType (&staticArrayData)[ArraySize])
{
    return Span<const uchar>(reinterpret_cast<const uchar*>(staticArrayData), ArraySize * sizeof(InType));
}

}

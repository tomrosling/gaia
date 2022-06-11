#pragma once

namespace gaia
{

/*
 * A constant buffer that is always mapped into CPU-accessible memory.
 * It is split/double-buffered to allow updating before rendering has completed for the previous frame.
 */
template<typename DataType>
class MappedConstantBuffer
{
public:
    // Take ownership and map the buffer. Note that the buffer must be sized TotalSize!
    void Create(ID3D12Resource* buffer);

    D3D12_GPU_VIRTUAL_ADDRESS GetBufferGPUVirtualAddress(int frame) { return m_buffer->GetGPUVirtualAddress() + frame * AlignedDataSize(); }
    DataType* GetMappedData(int frame) { return (DataType*)((uchar*)m_mappedData + frame * AlignedDataSize()); }

    // Align up to pad between read/write buffers and avoid GPU cache hazards.
    static size_t AlignedDataSize() { return math::RoundUpPow2(sizeof(DataType), (size_t)128); }
    static size_t TotalSize() { return AlignedDataSize() * BackbufferCount; }

private:
    ComPtr<ID3D12Resource> m_buffer;
    DataType* m_mappedData = nullptr;
};


template <typename DataType>
inline void MappedConstantBuffer<DataType>::Create(ID3D12Resource* buffer)
{
    Assert(buffer->GetDesc().Width == TotalSize());
    m_buffer = buffer;
    D3D12_RANGE readRange = {};
    buffer->Map(0, &readRange, (void**)&m_mappedData);
}

} // namespace gaia

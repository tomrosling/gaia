#include "GenerateMips.hpp"
#include "Renderer.hpp"

namespace gaia
{

struct GenerateMipsConstants
{
    uint32 srcMipLevel;
    uint32 numMipLevels;
    float32 texelSize;
};

namespace GenerateMipsRootParam
{
enum E
{
    GenerateMipsConstants,
    SrcMip,
    DstMip,
    Count
};
}

bool GenerateMips::Init(Renderer& renderer)
{
    if (!CreateRootSignature(renderer))
        return false;

    if (!CreatePipelineState(renderer))
        return false;

    return true;
}

void GenerateMips::Compute(Renderer& renderer, ID3D12Resource* texture)
{
    ID3D12Device& device = renderer.GetDevice();
    ID3D12GraphicsCommandList2& commandList = renderer.GetComputeCommandList();

    const D3D12_RESOURCE_DESC& desc = texture->GetDesc();
    Assert(desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);
    Assert(math::IsPow2(desc.Width) && desc.Height == desc.Width);
    Assert(desc.MipLevels > 1);
    Assert(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    commandList.SetComputeRootSignature(m_rootSignature.Get());
    commandList.SetPipelineState(m_pipelineState.Get());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;

    for (int srcMip = 0; srcMip < desc.MipLevels - 1; srcMip += 4)
    {
        int mipCount = std::min(4, desc.MipLevels - 1 - srcMip);
        uint32 srcWidth = desc.Width >> srcMip;
        uint32 dstWidth = srcWidth >> 1;

        GenerateMipsConstants constants = {};
        constants.srcMipLevel = srcMip;
        constants.numMipLevels = mipCount;
        constants.texelSize = 1.f / (float)dstWidth;

        commandList.SetComputeRoot32BitConstants(GenerateMipsRootParam::GenerateMipsConstants, sizeof(constants) / 4, &constants, 0);

        int srvDescIndex = renderer.AllocateComputeSRV(texture, srvDesc);
        renderer.BindComputeDescriptor(srvDescIndex, GenerateMipsRootParam::SrcMip);

        int uavDescIndices[4] = {};
        for (int mip = 0; mip < mipCount; ++mip)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = desc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = srcMip + mip + 1;

            uavDescIndices[mip] = renderer.AllocateComputeUAV(texture, uavDesc);
        }

        // Pad any unused mip levels with a default UAV. Doing this keeps the DX12 runtime happy, apparently.
        for (int mip = mipCount; mip < 4; ++mip)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = desc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = srcMip + mip + 1;

            uavDescIndices[mip] = renderer.AllocateComputeUAV(nullptr, uavDesc);
        }

        // Slight hack; we really should use an AllocateComputeUAVs(4) interface, but we know they're contiguous anyway for now.
        renderer.BindComputeDescriptor(uavDescIndices[0], GenerateMipsRootParam::DstMip);
        
        int numThreadGroups = (dstWidth + 7) / 8;
        commandList.Dispatch(numThreadGroups, numThreadGroups, 1);
        
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(texture);
        commandList.ResourceBarrier(1, &barrier);
    }
}

bool GenerateMips::CreateRootSignature(Renderer& renderer)
{
    ID3D12Device2& device = renderer.GetDevice();
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = renderer.GetRootSignatureFeaturedData();

    CD3DX12_DESCRIPTOR_RANGE1 srcMip(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
    CD3DX12_DESCRIPTOR_RANGE1 dstMip(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

    CD3DX12_ROOT_PARAMETER1 rootParameters[GenerateMipsRootParam::Count];
    rootParameters[GenerateMipsRootParam::GenerateMipsConstants].InitAsConstants(sizeof(GenerateMipsConstants) / 4, 0);
    rootParameters[GenerateMipsRootParam::SrcMip].InitAsDescriptorTable(1, &srcMip);
    rootParameters[GenerateMipsRootParam::DstMip].InitAsDescriptorTable(1, &dstMip);

    CD3DX12_STATIC_SAMPLER_DESC linearClampSampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(GenerateMipsRootParam::Count, rootParameters, 1, &linearClampSampler);
    ComPtr<ID3DBlob> rootSigBlob;
    ComPtr<ID3DBlob> errBlob;
    if (FAILED(::D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &rootSigBlob, &errBlob)))
    {
        DebugOut("Failed to create root signature: %s\n", errBlob->GetBufferPointer());
        return false;
    }

    if (FAILED(device.CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
        return false;

    return true;
}

bool GenerateMips::CreatePipelineState(Renderer& renderer)
{
    ID3D12Device2& device = renderer.GetDevice();

    // Load shader.
    ComPtr<ID3DBlob> shader = renderer.LoadCompiledShader(L"GenerateMips.cso");
    if (!shader)
        return false;

    // Create PSO.
    struct PipelineStateStream
    {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_CS computeShader;
    };

    PipelineStateStream stream;
    stream.rootSignature = m_rootSignature.Get();
    stream.computeShader = CD3DX12_SHADER_BYTECODE(shader.Get());

    D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
        sizeof(PipelineStateStream), &stream
    };

    if (FAILED(device.CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_pipelineState))))
        return false;

    return true;
}

}

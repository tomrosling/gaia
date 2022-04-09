#include "TerrainComputeNormals.hpp"
#include "Renderer.hpp"
#include "TerrainConstants.hpp"

namespace gaia
{

using namespace TerrainConstants;

struct ComputeNormalsConstants
{
    Vec2i uvMin;
    float worldTexelSizeTimes8;
};

namespace CalculateNormalsRootParam
{
enum E
{
    CalculateNormalsConstants,
    SrcHeightMap,
    DstNormalMap,
    Count
};
}

bool TerrainComputeNormals::Init(Renderer& renderer)
{
    if (!CreateRootSignature(renderer))
        return false;

    m_pipelineState = renderer.CreateComputePipelineState(L"TerrainComputeNormals.cso", *m_rootSignature.Get());
    if (!m_pipelineState)
        return false;

    return true;
}

void TerrainComputeNormals::Compute(Renderer& renderer, ID3D12Resource* srcHeightMap, ID3D12Resource* dstNormalMap, Vec2i uvMin, Vec2i uvMax, int level)
{
    // Validate inputs and ensure we don't wrap around the normalmap multiple times.
    Assert(uvMax.x > uvMin.x && uvMax.y > uvMin.y);
    uvMax = math::min(uvMax, uvMin + Vec2i(HeightmapDimension, HeightmapDimension));

    const int NumThreads = 8;

    ID3D12Device& device = renderer.GetDevice();
    ID3D12GraphicsCommandList2& commandList = renderer.GetComputeCommandList();

    // Bind PSO
    commandList.SetComputeRootSignature(m_rootSignature.Get());
    commandList.SetPipelineState(m_pipelineState.Get());

    // Bind constants.
    ComputeNormalsConstants constants = {};
    uvMin = math::RoundDownPow2(uvMin, Vec2i(NumThreads, NumThreads));
    uvMax = math::RoundUpPow2(uvMax, Vec2i(NumThreads, NumThreads));
    constants.uvMin = uvMin;
    constants.worldTexelSizeTimes8 = 8.f * TerrainConstants::TexelSize * float(1 << level);
    commandList.SetComputeRoot32BitConstants(CalculateNormalsRootParam::CalculateNormalsConstants, sizeof(constants) / 4, &constants, 0);

    // Bind textures
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = srcHeightMap->GetDesc().Format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    int srvDescIndex = renderer.AllocateComputeSRV(srcHeightMap, srvDesc);
    renderer.BindComputeDescriptor(srvDescIndex, CalculateNormalsRootParam::SrcHeightMap);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = dstNormalMap->GetDesc().Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    int uavDescIndex = renderer.AllocateComputeUAV(dstNormalMap, uavDesc);
    renderer.BindComputeDescriptor(uavDescIndex, CalculateNormalsRootParam::DstNormalMap);

    // Dispatch compute call
    Vec2i numThreadGroups = (uvMax - uvMin) / NumThreads;
    commandList.Dispatch(numThreadGroups.x, numThreadGroups.y, 1);

    // No UAV barrier needed here because we aren't reliant on this finishing before further writes to the normal map,
    // and the only reads will come from the rendering queue which waits for this job.
}

bool TerrainComputeNormals::CreateRootSignature(Renderer& renderer)
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = renderer.GetRootSignatureFeaturedData();

    CD3DX12_DESCRIPTOR_RANGE1 srcDesc(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
    CD3DX12_DESCRIPTOR_RANGE1 dstDesc(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

    CD3DX12_ROOT_PARAMETER1 rootParameters[CalculateNormalsRootParam::Count];
    rootParameters[CalculateNormalsRootParam::CalculateNormalsConstants].InitAsConstants(sizeof(ComputeNormalsConstants) / 4, 0);
    rootParameters[CalculateNormalsRootParam::SrcHeightMap].InitAsDescriptorTable(1, &srcDesc);
    rootParameters[CalculateNormalsRootParam::DstNormalMap].InitAsDescriptorTable(1, &dstDesc);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(CalculateNormalsRootParam::Count, rootParameters, 0, nullptr);
    ComPtr<ID3DBlob> rootSigBlob;
    ComPtr<ID3DBlob> errBlob;
    if (FAILED(::D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &rootSigBlob, &errBlob)))
    {
        DebugOut("Failed to create TerrainComputeNormals root signature: %s\n", errBlob->GetBufferPointer());
        return false;
    }

    if (FAILED(renderer.GetDevice().CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
        return false;

    return true;
}

} // namespace gaia

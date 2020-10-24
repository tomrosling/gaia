/**
 * Compute shader to generate mipmaps for a given texture.
 * Modified from source: https://github.com/Microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Core/Shaders/GenerateMipsCS.hlsli
 */

struct ComputeShaderInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};

cbuffer GenerateMipsCB : register( b0 )
{
    uint SrcMipLevel;	// Texture level of source mip
    uint NumMipLevels;	// Number of OutMips to write: [1-4]
    float TexelSize;	// 1.0 / OutMip1.Dimension
}

// Source mip map.
Texture2D<float4> SrcMip : register( t0 );

// Write up to 4 mip map levels.
RWTexture2D<float4> OutMip1 : register( u0 );
RWTexture2D<float4> OutMip2 : register( u1 );
RWTexture2D<float4> OutMip3 : register( u2 );
RWTexture2D<float4> OutMip4 : register( u3 );

// Linear clamp sampler.
SamplerState LinearClampSampler : register( s0 );

#define GenerateMips_RootSignature \
    "RootFlags(0), " \
    "RootConstants(b0, num32BitConstants = 3), " \
    "DescriptorTable(SRV(t0, numDescriptors = 1))," \
    "DescriptorTable(UAV(u0, numDescriptors = 4))," \
    "StaticSampler(s0," \
        "addressU = TEXTURE_ADDRESS_CLAMP," \
        "addressV = TEXTURE_ADDRESS_CLAMP," \
        "addressW = TEXTURE_ADDRESS_CLAMP," \
        "filter = FILTER_MIN_MAG_MIP_LINEAR)"

// The reason for separating channels is to reduce bank conflicts in the
// local data memory controller.  A large stride will cause more threads
// to collide on the same memory bank.
groupshared float gs_R[64];
groupshared float gs_G[64];
groupshared float gs_B[64];
groupshared float gs_A[64];

void StoreColor(uint Index, float4 Color)
{
    gs_R[Index] = Color.r;
    gs_G[Index] = Color.g;
    gs_B[Index] = Color.b;
    gs_A[Index] = Color.a;
}

float4 LoadColor(uint Index )
{
    return float4( gs_R[Index], gs_G[Index], gs_B[Index], gs_A[Index] );
}

float3 LinearToSRGB(float3 x)
{
    // This is exactly the sRGB curve
    //return x < 0.0031308 ? 12.92 * x : 1.055 * pow(abs(x), 1.0 / 2.4) - 0.055;

    // This is cheaper but nearly equivalent
    return x < 0.0031308 ? 12.92 * x : 1.13005 * sqrt(abs(x - 0.00228)) - 0.13448 * x + 0.005719;
}

float4 PackColor(float4 Linear)
{
#if defined(CONVERT_TO_SRGB)
    return float4(LinearToSRGB(Linear.rgb), Linear.a);
#else
    return Linear;
#endif
}

[RootSignature(GenerateMips_RootSignature)]
[numthreads(8, 8, 1)]
void main(ComputeShaderInput IN)
{
	uint srcWidth, srcHeight, numMipLevels;
	SrcMip.GetDimensions(SrcMipLevel, srcWidth, srcHeight, numMipLevels);
	
	float2 UV = TexelSize * (IN.DispatchThreadID.xy + 0.5);
	float4 Src1 = SrcMip.SampleLevel(LinearClampSampler, UV, SrcMipLevel);

    OutMip1[IN.DispatchThreadID.xy] = PackColor( Src1 );

    // A scalar (constant) branch can exit all threads coherently.
    if ( NumMipLevels == 1 )
        return;

    // Without lane swizzle operations, the only way to share data with other
    // threads is through LDS.
    StoreColor( IN.GroupIndex, Src1 );

    // This guarantees all LDS writes are complete and that all threads have
    // executed all instructions so far (and therefore have issued their LDS
    // write instructions.)
    GroupMemoryBarrierWithGroupSync();

    // With low three bits for X and high three bits for Y, this bit mask
    // (binary: 001001) checks that X and Y are even.
    if ( ( IN.GroupIndex & 0x9 ) == 0 )
    {
        float4 Src2 = LoadColor( IN.GroupIndex + 0x01 );
        float4 Src3 = LoadColor( IN.GroupIndex + 0x08 );
        float4 Src4 = LoadColor( IN.GroupIndex + 0x09 );
        Src1 = 0.25 * ( Src1 + Src2 + Src3 + Src4 );

        OutMip2[IN.DispatchThreadID.xy / 2] = PackColor( Src1 );
        StoreColor( IN.GroupIndex, Src1 );
    }

    if ( NumMipLevels == 2 )
        return;

    GroupMemoryBarrierWithGroupSync();

    // This bit mask (binary: 011011) checks that X and Y are multiples of four.
    if ( ( IN.GroupIndex & 0x1B ) == 0 )
    {
        float4 Src2 = LoadColor( IN.GroupIndex + 0x02 );
        float4 Src3 = LoadColor( IN.GroupIndex + 0x10 );
        float4 Src4 = LoadColor( IN.GroupIndex + 0x12 );
        Src1 = 0.25 * ( Src1 + Src2 + Src3 + Src4 );

        OutMip3[IN.DispatchThreadID.xy / 4] = PackColor( Src1 );
        StoreColor( IN.GroupIndex, Src1 );
    }

    if ( NumMipLevels == 3 )
        return;

    GroupMemoryBarrierWithGroupSync();

    // This bit mask would be 111111 (X & Y multiples of 8), but only one
    // thread fits that criteria.
    if ( IN.GroupIndex == 0 )
    {
        float4 Src2 = LoadColor( IN.GroupIndex + 0x04 );
        float4 Src3 = LoadColor( IN.GroupIndex + 0x20 );
        float4 Src4 = LoadColor( IN.GroupIndex + 0x24 );
        Src1 = 0.25 * ( Src1 + Src2 + Src3 + Src4 );

        OutMip4[IN.DispatchThreadID.xy / 8] = PackColor( Src1 );
    }
}

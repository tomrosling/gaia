cbuffer PSSharedConstants : register(b1)
{
    float3 CamPos;
};

struct VertexShaderOutput
{
    float2 pos : POSITION;
};

struct HullShaderControlPointOutput
{
    float2 pos : POSITION;
};
 
struct HullShaderConstantOutput
{
    float EdgeTessFactor[4]     : SV_TessFactor;
    float InsideTessFactor[2]   : SV_InsideTessFactor;
};
 
typedef InputPatch<VertexShaderOutput, 4> InPatch;

float CalcTessFactor(float2 pos)
{
    // Scale from 1-64 exponentially over the range 1m-64m.
    const float MinDist = 1.0;
    const float MaxDist = 512.0;
    float dist = distance(CamPos, float3(pos.x, 0.0, pos.y));
    return clamp(MaxDist / dist, 1.0, 64.0);
}
 
// Patch Constant Function
HullShaderConstantOutput CalcHSPatchConstants(InPatch ip, uint PatchID : SV_PrimitiveID)
{
    HullShaderConstantOutput output;

    float2 centre = 0.25 * (ip[0].pos + ip[1].pos + ip[2].pos + ip[3].pos);
    float centreFactor = CalcTessFactor(centre);

    output.EdgeTessFactor[0] = centreFactor;
    output.EdgeTessFactor[1] = centreFactor;
    output.EdgeTessFactor[2] = centreFactor;
    output.EdgeTessFactor[3] = centreFactor;

    output.InsideTessFactor[0] = centreFactor;
    output.InsideTessFactor[1] = centreFactor;
 
    return output;
}
 
[domain("quad")]
[partitioning("fractional_even")]
[outputtopology("triangle_ccw")]
[outputcontrolpoints(2)]
[patchconstantfunc("CalcHSPatchConstants")]
HullShaderControlPointOutput main(InPatch ip, uint i : SV_OutputControlPointID, uint PatchID : SV_PrimitiveID)
{
    HullShaderControlPointOutput output;
 
    // Output the AABB of the patch as a min/max pair.
    float2 candidates[2] = { ip[0].pos, ip[3].pos };
    output.pos = candidates[i];
 
    return output;
}

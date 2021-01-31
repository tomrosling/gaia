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
 
// Patch Constant Function
HullShaderConstantOutput CalcHSPatchConstants(InPatch ip, uint PatchID : SV_PrimitiveID)
{
    HullShaderConstantOutput output;
 
    // Insert code to compute output here
    output.EdgeTessFactor[0] = 4;
    output.EdgeTessFactor[1] = 4;
    output.EdgeTessFactor[2] = 4;
    output.EdgeTessFactor[3] = 4;
    output.InsideTessFactor[0] = 4; 
    output.InsideTessFactor[1] = 4; 
 
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

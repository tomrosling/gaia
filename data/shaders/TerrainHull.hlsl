cbuffer PSSharedConstants : register(b1)
{
    float3 CamPos;
    float3 SunDirection;
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
    // Scale from 1-64 exponentially over the range 1m-512m.
    const float MinDist = 1.0;
    const float MaxDist = 512.0;
    float dist = distance(CamPos, float3(pos.x, 0.0, pos.y));
    return clamp(MaxDist / dist, 1.0, 64.0);
}

// Patch Constant Function
HullShaderConstantOutput CalcHSPatchConstants(InPatch ip, uint PatchID : SV_PrimitiveID)
{
    HullShaderConstantOutput output;

    // Get the midpoint of each edge to find the tesselation factor for that edge.
    // Could also improve this by sampling the heightmap here too to get a better idea of screen coverage.
    float2 edge0 = (ip[0].pos + ip[2].pos) * 0.5;
    float2 edge1 = lerp(ip[0].pos, ip[1].pos, 0.5); // For some reason if this is 0.5 * (a + b), it ends up at the centre of the patch. Compiler bug?
    float2 edge2 = (ip[1].pos + ip[3].pos) * 0.5;
    float2 edge3 = (ip[2].pos + ip[3].pos) * 0.5;
    float2 centre = (ip[0].pos + ip[1].pos + ip[2].pos + ip[3].pos) * 0.25;

    output.EdgeTessFactor[0] = CalcTessFactor(edge0);
    output.EdgeTessFactor[1] = CalcTessFactor(edge1);
    output.EdgeTessFactor[2] = CalcTessFactor(edge2);
    output.EdgeTessFactor[3] = CalcTessFactor(edge3);
    
    float centreFactor = CalcTessFactor(centre);
    output.InsideTessFactor[0] = centreFactor;
    output.InsideTessFactor[1] = centreFactor;
 
    return output;
}
 
[domain("quad")]
[partitioning("fractional_odd")]
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

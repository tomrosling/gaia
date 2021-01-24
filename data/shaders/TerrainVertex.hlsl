cbuffer VSSharedConstants : register(b0)
{
    matrix viewMat;
    matrix projMat;
};
 
struct Vertex
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
};
 
struct VertexShaderOutput
{
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
    float4 pos : SV_Position;
};
 
VertexShaderOutput main(Vertex IN)
{
    VertexShaderOutput OUT;
 
    OUT.pos = mul(projMat, mul(viewMat, float4(IN.pos, 1.0)));

    OUT.worldPos = IN.pos;
    OUT.nrm = IN.nrm;
 
    return OUT;
}

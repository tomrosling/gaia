cbuffer Constants : register(b0)
{
    matrix ViewProjMat;
};
 
struct VertexPosColor
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float4 col : COLOUR;
};
 
struct VertexShaderOutput
{
    float3 worldPos : POSITION;
    float3 nrm : NORMAL;
    float4 col : COLOUR;
    float4 pos : SV_Position;
};
 
VertexShaderOutput main(VertexPosColor IN)
{
    VertexShaderOutput OUT;
 
    OUT.pos = mul(ViewProjMat, float4(IN.pos, 1.0));

    OUT.worldPos = IN.pos;
    OUT.nrm = IN.nrm;
    OUT.col = IN.col;
 
    return OUT;
}

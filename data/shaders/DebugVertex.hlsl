cbuffer VSSharedConstants : register(b0)
{
    matrix viewMat;
    matrix projMat;
};
 
struct VertexPosColor
{
    float3 pos : POSITION;
    float4 col : COLOUR;
};
 
struct VertexShaderOutput
{
    float4 col : COLOUR;
    float4 pos : SV_Position;
};
 
VertexShaderOutput main(VertexPosColor IN)
{
    VertexShaderOutput OUT;
 
    OUT.pos = mul(projMat, mul(viewMat, float4(IN.pos, 1.0)));
    OUT.col = IN.col;
 
    return OUT;
}

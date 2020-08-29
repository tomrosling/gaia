cbuffer Constants : register(b0)
{
    matrix MvpMatrix;
};
 
struct VertexPosColor
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float3 col : COLOUR;
};
 
struct VertexShaderOutput
{
    float3 modelpos : POSITION;
    float3 nrm : NORMAL;
    float3 col : COLOUR;
    float4 pos : SV_Position;
};
 
VertexShaderOutput main(VertexPosColor IN)
{
    VertexShaderOutput OUT;
 
    OUT.pos = mul(MvpMatrix, float4(IN.pos, 1.0));

    // TODO: Transform IN.nrm into world(?) space
    OUT.modelpos = IN.pos;
    OUT.nrm = IN.nrm;

    OUT.col = IN.col;
 
    return OUT;
}

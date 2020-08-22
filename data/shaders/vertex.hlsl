struct VertexPosColor
{
    float3 pos : POSITION;
    float3 col : COLOUR;
};
 
struct VertexShaderOutput
{
    float4 col : COLOUR;
    float4 pos : SV_Position;
};
 
VertexShaderOutput main(VertexPosColor IN)
{
    VertexShaderOutput OUT;
 
    //OUT.Position = mul(ModelViewProjectionCB.MVP, float4(IN.Position, 1.0f));
    OUT.pos = float4(IN.pos, 1.0f);
    OUT.col = float4(IN.col, 1.0f);
 
    return OUT;
}

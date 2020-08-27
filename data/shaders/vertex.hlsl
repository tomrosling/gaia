struct ModelViewProjection
{
    matrix mvp;
};
 
ConstantBuffer<ModelViewProjection> ModelViewProjectionCB : register(b0);
 
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
 
    OUT.pos = mul(ModelViewProjectionCB.mvp, float4(IN.pos, 1.0f));
    OUT.col = float4(IN.col, 1.0f);
 
    return OUT;
}

struct GaiaConstantsT
{
    matrix mvpMatrix;
    float3 camPos;
};
 
ConstantBuffer<GaiaConstantsT> GaiaConstants : register(b0);
 
struct VertexPosColor
{
    float3 pos : POSITION;
    float3 nrm : NORMAL;
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
 
    OUT.pos = mul(GaiaConstants.mvpMatrix, float4(IN.pos, 1.f));

    // TODO: Transform IN.nrm into world(?) space
    
    // "Headlight" behaviour: light dir == view dirw
    float3 viewDir = normalize(GaiaConstants.camPos - IN.pos);
    float ndotv = dot(IN.nrm, viewDir);
    OUT.col = float4(ndotv * IN.col, 1.0f);
 
    return OUT;
}

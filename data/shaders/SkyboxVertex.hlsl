cbuffer VSSharedConstants : register(b0)
{
    matrix viewMat;
    matrix projMat;
};

struct SkyboxVertex
{
    float3 pos : POSITION;
};
 
struct VertexShaderOutput
{
    float3 uvw : POSITION;
    float4 pos : SV_Position;
};
 
VertexShaderOutput main(SkyboxVertex IN)
{
    VertexShaderOutput OUT;

    // Rotate with the view, but don't translate our cube.
    float3 viewPos = mul((float3x3)viewMat, IN.pos);

    // Force z == w so the skybox is on the far clip plane.
    OUT.pos = mul(projMat, float4(viewPos, 1.0)).xyww;
    
    OUT.uvw = IN.pos;
 
    return OUT;
}

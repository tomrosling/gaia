struct Vertex
{
    float2 pos : POSITION;
};

struct VertexShaderOutput
{
    float2 pos : POSITION;
};

 
VertexShaderOutput main(Vertex IN)
{
    VertexShaderOutput OUT;
    OUT.pos = IN.pos; 
    return OUT;
}

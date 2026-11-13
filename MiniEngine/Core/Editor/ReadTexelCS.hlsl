cbuffer Constants : register(b0)
{
    int2 Texel;
};

Texture2D<float4> Source : register(t0);
RWStructuredBuffer<float4> TexelValue : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    TexelValue[0] = Source[Texel];
}

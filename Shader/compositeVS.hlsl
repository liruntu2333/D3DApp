#include "composite.hlsli"

VertexOut main(uint vid : SV_VertexID)
{
    VertexOut vout;

    vout.TexC = gTexCoords[vid];

    // [0, 1]^2 to [-1, 1] * [-1, 1] * [0, 1]
    vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);

    return vout;
}
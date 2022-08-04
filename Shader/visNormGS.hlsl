#include "visNorm.hlsli"

[maxvertexcount(2)]
void main(
	point VertexOut gin[1], 
	inout LineStream< GeoOut > output
)
{
    GeoOut gout[2];
    gout[0].PosH = mul(float4(gin[0].PosW, 1.0f), gViewProj);
    float3 endPosW = gin[0].PosW + gin[0].NormalW;
    gout[1].PosH = mul(float4(endPosW, 1.0f), gViewProj);
    gout[0].Color = gin[0].NormalW;
    gout[1].Color = gin[0].NormalW;

    output.Append(gout[0]);
    output.Append(gout[1]);
    output.RestartStrip();
}
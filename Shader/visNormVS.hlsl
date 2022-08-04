#include "visNorm.hlsli"

VertexOut main( VertexIn vin )
{
    VertexOut vout;
    vout.NormalW = mul(float4(vin.NormalL, 0.0f), gWorldInvT).xyz;
    vout.PosW = mul(float4(vin.PosL, 1.0f), gWorld).xyz;
    return vout;
}
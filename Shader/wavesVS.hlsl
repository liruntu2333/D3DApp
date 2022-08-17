#include "default.hlsli"

VertexOut main( VertexIn vin )
{
    VertexOut vout;

    vin.PosL.y += gDisplacementMap.SampleLevel(gSamLinearWrap, vin.TexC, 1.0f).r;

    float du = gDisplacementMapTexelSize.x;
    float dv = gDisplacementMapTexelSize.y;

    const float lft = gDisplacementMap.SampleLevel(gSamPointClamp, vin.TexC - float2(du, 0.0f), 0.0f).r;
    const float rht = gDisplacementMap.SampleLevel(gSamPointClamp, vin.TexC + float2(du, 0.0f), 0.0f).r;
    const float top = gDisplacementMap.SampleLevel(gSamPointClamp, vin.TexC - float2(0.0f, dv), 0.0f).r;
    const float btm = gDisplacementMap.SampleLevel(gSamPointClamp, vin.TexC + float2(0.0f, dv), 0.0f).r;
    vin.NormalL = normalize(float3(lft - rht, 2.0f * gGridSpatialStep, btm - top));

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);

    vout.PosH = mul(posW, gViewProj);

    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;

    return vout;
}
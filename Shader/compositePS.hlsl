#include "composite.hlsli"

float4 main(const VertexOut pin) : SV_TARGET
{
	const float4 c = gBaseMap.SampleLevel(gSamPointClamp, pin.TexC, 0.0f);
	const float4 e = gEdgeMap.SampleLevel(gSamPointClamp, pin.TexC, 0.0f);

    //return c * e;
    return c;
}
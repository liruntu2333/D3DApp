#include "tessellation.hlsli"

[domain("quad")]
DomainOut main(
	PatchTess pt,
	float2 uv : SV_DomainLocation,
	const OutputPatch<HullOut, 4> quad)
{
	DomainOut dout;

    float3 v1 = lerp(quad[0].PosL, quad[1].PosL, uv.x);
    float3 v2 = lerp(quad[2].PosL, quad[3].PosL, uv.x);
    float3 p = lerp(v1, v2, uv.y);

    p.y = 0.3f * (p.z * sin(0.1f * p.x) + p.x * cos(0.1f * p.z));

    float4 posW = mul(float4(p, 1.0f), gWorld);

    const float nx = -0.03f * posW.z * cos(0.1f * posW.x) - 0.3f * cos(posW.z);
    const float ny = 1.0f;
    const float nz = -0.3f * sin(0.1f * posW.x) + 0.03f * posW.x * sin(0.1f * posW.z);
	const float3 nW = normalize(float3(nx, ny, nz));

    const float4 texC = mul(float4(uv, 0.0f, 1.0f), gTexTransform);
	
	dout.PosW = posW.xyz;
    dout.PosH = mul(posW, gViewProj);
    dout.NormalW = nW;
    dout.TexC = mul(texC, gMatTransform).xy;
	
	return dout;
}

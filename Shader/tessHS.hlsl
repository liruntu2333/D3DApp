#include "tessellation.hlsli"

PatchTess CalcHSPatchConstants(
	InputPatch<VertexOut, 4> ip,
	uint PatchID : SV_PrimitiveID)
{
	PatchTess op;

    float3 centerL = 0.25f * (ip[0].PosL + ip[1].PosL + ip[2].PosL + ip[3].PosL);
    float3 centerW = mul(float4(centerL, 1.0f), gWorld).xyz;

    float d = distance(centerW, gEyePosW);

    const float d0 = 20.0f;
    const float d1 = 200.0f;
    float tess = 64.0f * saturate((d1 - d) / (d1 - d0));

	op.EdgeTess[0] = 
		op.EdgeTess[1] = 
		op.EdgeTess[2] = 
		op.EdgeTess[3] = 
		tess;

    op.InsideTess[0] =
		op.InsideTess[1] =
		tess;

	return op;
}

[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("CalcHSPatchConstants")]
[maxtessfactor(64.0f)]
HullOut main(
	InputPatch<VertexOut, 4> ip,
	uint i : SV_OutputControlPointID,
	uint PatchID : SV_PrimitiveID )
{
	HullOut hout;

    hout.PosL = ip[i].PosL;

	return hout;
}

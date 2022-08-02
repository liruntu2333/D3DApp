#include "treeSprite.hlsli"

[maxvertexcount(4)]
void main(point VertexOut gin[1],
	uint primId : SV_PrimitiveID,
	inout TriangleStream< GeoOut > triStream)
{
	const float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 eye = gEyePosW - gin[0].CenterW;
    eye.y = 0.0f;
    eye = normalize(eye);
    // hlsl cross product results a left-handed coordinate
    float3 rht = cross(up, eye); // right at billboard's perspective, which means camera's left

    float halfWidth  = 0.5f * gin[0].SizeW.x;
    float halfHeight = 0.5f * gin[0].SizeW.y;

    float4 v[4] =
    {
        float4(gin[0].CenterW + halfWidth * rht - halfHeight * up, 1.0f),
		float4(gin[0].CenterW + halfWidth * rht + halfHeight * up, 1.0f),
	    float4(gin[0].CenterW - halfWidth * rht - halfHeight * up, 1.0f),
		float4(gin[0].CenterW - halfWidth * rht + halfHeight * up, 1.0f),
    };

    const float2 texC[4] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f),
    };

    for (int i = 0; i < 4; ++i)
    {
	    GeoOut gout;
	    gout.PosH    = mul(v[i], gViewProj);
        gout.PosW    = v[i].xyz;
        gout.NormalW = eye;
        gout.TexC    = texC[i];
        gout.PrimId  = primId;

        triStream.Append(gout);
    }
}
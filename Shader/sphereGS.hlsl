#include "sphere.hlsli"

void Transform (VertexOut v, out GeoOut gout)
{
    gout.PosW    = mul(float4(v.PosL, 1.0f), gWorld).xyz;
    gout.NormalW = mul(v.NormalL, (float3x3) gWorld);
    gout.PosH    = mul(float4(gout.PosW, 1.0f), gViewProj);
    gout.TexC    = v.TexC;
}

void Subdivide(VertexOut inVert[3], out VertexOut outVert[6])
{
	//       v1
	//       *
	//      / \
	//     /   \
	//  m0*-----*m1
	//   / \   / \
	//  /   \ /   \
	// *-----*-----*
	// v0    m2     v2

    VertexOut m[3];
    m[0].PosL = 0.5f * (inVert[0].PosL + inVert[1].PosL);
    m[1].PosL = 0.5f * (inVert[1].PosL + inVert[2].PosL);
    m[2].PosL = 0.5f * (inVert[2].PosL + inVert[0].PosL);

    m[0].NormalL = normalize(m[0].PosL);
    m[1].NormalL = normalize(m[1].PosL);
    m[2].NormalL = normalize(m[2].PosL);

    m[0].PosL = m[0].NormalL * SPHERE_R;
    m[1].PosL = m[1].NormalL * SPHERE_R;
    m[2].PosL = m[2].NormalL * SPHERE_R;

    m[0].TexC = 0.5f * (inVert[0].TexC + inVert[1].TexC);
    m[1].TexC = 0.5f * (inVert[1].TexC + inVert[2].TexC);
    m[2].TexC = 0.5f * (inVert[2].TexC + inVert[0].TexC);

    outVert[0] = inVert[0];
    outVert[1] = m[0];
    outVert[2] = m[2];
    outVert[3] = m[1];
    outVert[4] = inVert[2];
    outVert[5] = inVert[1];
}

void OutputSubdivide(VertexOut v[6], inout TriangleStream< GeoOut > output)
{
    GeoOut gout[6];

    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        Transform(v[i], gout[i]);
    }

   	//  m0*-----*m1
	//   / \   / \
	//  /   \ /   \
	// *-----*-----*
	// v0    m2     v2
    [unroll]
    for (int j = 0; j < 5; ++j)
    {
        output.Append(gout[j]);
    }
    output.RestartStrip();

	//       v1
	//       *
	//      / \
	//     /   \
	//  m0*-----*m1
    output.Append(gout[1]);
    output.Append(gout[5]);
    output.Append(gout[3]);
    output.RestartStrip();
}

[maxvertexcount(32)]
void main(
	triangle VertexOut input[3], 
	inout TriangleStream< GeoOut > output
)
{
    float3 objPosW = float3(gWorld[3][0], gWorld[3][1], gWorld[3][2]);
    float eyeDis = length(gEyePosW - objPosW);

    if (eyeDis < 30.0f)
    {
        VertexOut sub1[6];
        Subdivide(input, sub1);
        VertexOut sub2[6];
        VertexOut tri[3];

        [unroll]
        for (int i = 0; i < 3; ++i)
        {

            tri[0] = sub1[i];
            tri[1] = sub1[i + 1];
            tri[2] = sub1[i + 2];
            Subdivide(tri, sub2);
            OutputSubdivide(sub2, output);
        }

        tri[0] = sub1[1];
        tri[1] = sub1[5];
        tri[2] = sub1[3];
        Subdivide(tri, sub2);
        OutputSubdivide(sub2, output);
    }
    else if (eyeDis < 60.0f)
    {
        VertexOut v[6];
        Subdivide(input, v);
        OutputSubdivide(v, output);
    }
    else if (eyeDis >= 60.0f)
    {
        GeoOut gout[3];

        [unroll]
        for (int i = 0; i < 3; ++i)
        {
            Transform(input[i], gout[i]);
            output.Append(gout[i]);
        }
    }
}
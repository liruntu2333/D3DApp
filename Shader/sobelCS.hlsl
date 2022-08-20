Texture2D gInput            : register(t0);
RWTexture2D<float4> gOutput : register(u0);

float CalcLuminance(float3 color)
{
    return dot(color, float3(0.2999f, 0.598f, 0.114f));
}

[numthreads(16, 16, 1)]
void main( int3 dispatchThreadId : SV_DispatchThreadID )
{
    float4 c[3][3];
    for (int i = 0; i < 3; ++i)
    {
	    for (int j = 0; j < 3; ++j)
	    {
		    const int2 xy = dispatchThreadId.xy + int2(j - 1, -i - 1);
            c[i][j] = gInput[xy];
        }
    }

    //          [ -1  0  +1 ]
    //  G(x) =  [ -2  0  +2 ]  * A
    //          [ -1  0  +1 ]
    const float4 partialDiffX = -1.0f * c[0][0] - 2.0f * c[1][0] - 1.0f * c[2][0]
								+1.0f * c[0][2] + 2.0f * c[1][2] + 1.0f * c[2][2];

	//          [ +1  +2  +1 ]
    //  G(y) =  [  0   0   0 ]  * A
    //          [ -1  -2  -1 ]
    const float4 partialDiffY = -1.0f * c[2][0] - 2.0f * c[2][1] - 1.0f * c[2][1]
								+1.0f * c[0][0] + 2.0f * c[0][1] + 1.0f * c[0][2];

    float4 mag = sqrt(partialDiffX * partialDiffX + partialDiffY * partialDiffY);
    mag = 1.0f - saturate(CalcLuminance(mag.rgb));
    gOutput[dispatchThreadId.xy] = mag;
}
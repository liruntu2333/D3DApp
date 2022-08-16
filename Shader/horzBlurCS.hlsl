#include "blur.hlsli"

[numthreads(GROUP_N, 1, 1)]
void main( 
	int3 groupThreadId    : SV_GroupThreadID,
	int3 dispatchThreadId : SV_DispatchThreadID)
{
	float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, };

	// For each group : e.g. N == 256 R == 4
	// gInput	[       prevGroup       cccc][cccccccc currGroup cccccc][cccc     nextGroup        ]
	//									||||  |||||||||||||||||||||||||  ||||
	//									vvvv  vvvvvvvvvvvvvvvvvvvvvvvvv  vvvv
	// gCache						   [cccc  ccccccccccccccccccccccccc  cccc]
	// groupThreadId					0123  01234................2222	 2222
	//															   5555  5555
	//															   2345  2345

    if (groupThreadId.x < gBlurRadius)
    {
        uint x = max(dispatchThreadId.x - gBlurRadius, 0);
        gCache[groupThreadId.x] = gInput[int2(x, dispatchThreadId.y)];
    }
	else if (groupThreadId.x >= GROUP_N - gBlurRadius)
    {
        uint x = min(dispatchThreadId.x + gBlurRadius, gInput.Length.x - 1);
        gCache[groupThreadId.x + 2 * gBlurRadius] = gInput[int2(x, dispatchThreadId.y)];
    }
    
    gCache[groupThreadId.x + gBlurRadius] = gInput[min(dispatchThreadId.xy, gInput.Length.xy - 1)];

    GroupMemoryBarrierWithGroupSync();

    // blur pixels

    float4 blurColor = float4(0, 0, 0, 0);
    float3 centerColor = gCache[groupThreadId.x + gBlurRadius].xyz;
    float sigmaR2 = gSigmaR * gSigmaR;

	// generate range weight
    float weightSum = 0.0f;
    for (int i = -gBlurRadius; i <= gBlurRadius; ++i)
    {
    	// G(x) = exp(- (currCol - ctrCol)^2 / (2 * sigma ^ 2))
        float colorDis = length(gCache[groupThreadId.x + gBlurRadius + i].xyz - centerColor);
        float rangeWeight = exp(- colorDis * colorDis * 0.5f / sigmaR2);
        weights[i + gBlurRadius] *= rangeWeight;
        weightSum += weights[i + gBlurRadius];
    }

    for (int j = -gBlurRadius; j <= gBlurRadius; ++j)
    {
        weights[j + gBlurRadius] /= weightSum;
    }

    for (int k = -gBlurRadius; k <= gBlurRadius; ++k)
    {
        blurColor += weights[k + gBlurRadius] * gCache[groupThreadId.x + gBlurRadius + k];
    }

    gOutput[dispatchThreadId.xy] = blurColor;
}
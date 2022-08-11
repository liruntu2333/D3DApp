#include "blur.hlsli"

[numthreads(GROUP_N, 1, 1)]
void main( 
	int3 groupThreadId    : SV_GroupThreadID,
	int3 dispatchThreadId : SV_DispatchThreadID)
{
	const float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10, };

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

    for (int i = -gBlurRadius; i <= gBlurRadius; ++i)
    {
        blurColor += weights[i + gBlurRadius] * gCache[groupThreadId.x + gBlurRadius + i];
    }

    gOutput[dispatchThreadId.xy] = blurColor;
}
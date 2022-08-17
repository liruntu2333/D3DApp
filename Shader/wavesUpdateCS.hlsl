#include "waveSim.hlsli"

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int x = DTid.x;
    int y = DTid.y;

    gOutput[int2(x, y)] =
		gWaveConstant0 * gPrevSolInput[int2(x, y)].r +
		gWaveConstant1 * gCurrSolInput[int2(x, y)].r +
		gWaveConstant2 * (gCurrSolInput[int2(x, y + 1)].r +
						  gCurrSolInput[int2(x, y - 1)].r +
						  gCurrSolInput[int2(x + 1, y)].r +
						  gCurrSolInput[int2(x - 1, y)].r);
}
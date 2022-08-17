#include "waveSim.hlsli"

[numthreads(1, 1, 1)]
void main()
{
    int x = gDisturbIndex.x;
    int y = gDisturbIndex.y;

    const float halfMag = 0.5f * gDisturbMag;

    gOutput[int2(x, y)]     += gDisturbMag;
    gOutput[int2(x + 1, y)] += halfMag;
    gOutput[int2(x - 1, y)] += halfMag;
    gOutput[int2(x, y + 1)] += halfMag;
    gOutput[int2(x, y - 1)] += halfMag;
}
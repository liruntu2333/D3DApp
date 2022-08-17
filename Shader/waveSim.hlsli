cbuffer cbUpdateSettings
{
    float gWaveConstant0;
	float gWaveConstant1;
	float gWaveConstant2;

    float gDisturbMag;
    int2 gDisturbIndex;
}

RWTexture2D<float> gPrevSolInput : register(u0);
RWTexture2D<float> gCurrSolInput : register(u1);
RWTexture2D<float> gOutput       : register(u2);
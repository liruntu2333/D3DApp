#include "visNorm.hlsli"

float4 main( GeoOut vin ) : SV_TARGET
{
    float4 outColor = float4(vin.Color, 1.0f);
	return outColor;
}
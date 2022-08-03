#include "sphere.hlsli"

VertexOut main( VertexIn vin )
{
    VertexOut vout;
    vout.PosL    = vin.PosL;
    vout.NormalL = vin.NormalL;
    vout.TexC    = vin.TexC;
	return vout;
}
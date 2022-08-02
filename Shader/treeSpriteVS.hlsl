#include "treeSprite.hlsli"

VertexOut main(VertexIn vin)
{
    VertexOut vout = (VertexOut)0;

    vout.CenterW   = vin.PosW;
    vout.SizeW     = vin.SizeW;

    return vout;
}
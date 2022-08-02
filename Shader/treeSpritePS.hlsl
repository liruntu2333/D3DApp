#include "treeSprite.hlsli"

float4 main(GeoOut pin) : SV_TARGET
{
    float3 uvw = float3(pin.TexC, pin.PrimId % TREE_ARRAY_SIZE);
    float4 diffuseAlbedo = gTreeMapArray.Sample(gSamAnisotropicWrap, uvw) * gDiffuseAlbedo;

#ifdef ALPHA_TEST
    clip(diffuseAlbedo.a - 0.1f);
#endif

    pin.NormalW = normalize(pin.NormalW);

    float3 eyeDir = gEyePosW - pin.PosW;
    float eyeDis = length(eyeDir);
    eyeDir /= eyeDis;

    float4 ambient = gAmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - gRoughness;
    Material mat = { diffuseAlbedo, gFresnelR0, shininess };
    float3 shadowFactor = 1.0f;

    float4 directLight = ComputeLighting(gLights, mat, pin.PosW, pin.NormalW, eyeDir, shadowFactor);

    float4 outColor = ambient + directLight;

#ifdef FOG
    float fogConc = saturate((eyeDis - gFogStart) / gFogRange);
    outColor = lerp(outColor, gFogColor, fogConc);
#endif

    outColor.a = diffuseAlbedo.a;

    return outColor;
}
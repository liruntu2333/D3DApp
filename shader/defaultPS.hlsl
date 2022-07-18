#include "default.hlsli"

float4 main(VertexOut pin) : SV_Target
{
    float4 diffuseAlbedo = gDiffuseMap.Sample(gSamAnisotropicWrap, pin.TexC) * gDiffuseAlbedo;

    pin.NormalW = normalize(pin.NormalW);
    float3 eyeDir = normalize(gEyePosW - pin.PosW);

    float4 ambient = gAmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - gRoughness;
    Material mat = { diffuseAlbedo, gFresnelR0, shininess };
    float3 shadowFactor = 1.0f;

    float4 directLight = ComputeLighting(gLights, mat, pin.PosW, pin.NormalW, eyeDir, shadowFactor);

    float4 outColor = ambient + directLight;

    outColor.a = diffuseAlbedo.a;

    return outColor;
}
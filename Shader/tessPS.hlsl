#include  "tessellation.hlsli"

float4 main(DomainOut din) : SV_Target
{
    float4 diffuseAlbedo = gDiffuseMap.Sample(gSamAnisotropicWrap, din.TexC) * gDiffuseAlbedo;

    din.NormalW = normalize(din.NormalW);

    float3 eyeDir = gEyePosW - din.PosW;
    float eyeDis = length(eyeDir);
    eyeDir /= eyeDis;

    float4 ambient = gAmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - gRoughness;
    Material mat = { diffuseAlbedo, gFresnelR0, shininess };
    float3 shadowFactor = 1.0f;

    float4 directLight = ComputeLighting(gLights, mat, din.PosW, din.NormalW, eyeDir, shadowFactor);

    float4 outColor = ambient + directLight;

#ifdef FOG
    float fogConc = saturate((eyeDis - gFogStart) / gFogRange);
    outColor = lerp(outColor, gFogColor, fogConc);
#endif

    outColor.a = diffuseAlbedo.a;

    return outColor;
}
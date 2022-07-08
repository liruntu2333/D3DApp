#define LIGHT_MAX 16

struct Light
{
    float3 Intensity;
    float  AttnStart;
    float3 Direction;
    float  AttnEnd;
    float3 Position;
    float  SpotPower;
};

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float  Shininess;
};

// lerp attenuation coefficient
float CalAttenuation(float dis, float start, float end)
{
    return saturate((end - dis) / (end - start));
}

// R = R0 + (1 - R0) (1 - cosTheta)^5
float3 SchlickFresnel(float3 R0, float3 normal, float3 eyeDir)
{
	const float f0 = 1.0f - saturate(dot(normal, eyeDir));
    return R0 + (1.0f - R0) * f0 * f0 * f0 * f0 * f0;
}

float ToonShadingKs(float ks)
{
    return ks <= 0.0f ? 0.4f : (ks <= 0.5f ? 0.5f : (ks <= 1.0 ? 0.8f : ks));
}

float ToonShadingKd(float kd)
{
    return kd == 0.0f ? 0.2f : (kd <= 0.5f ? 0.4f : 0.8f);
}

// L(direct) = diffuse + specular
//           = cosTheta * I(direct) x (md + Rf * (m + 8)/8*(n*h)^m)
//                Kd                                     Ks
float3 BlinnPhong(float3 intensity, float3 lightDir, float3 norm, float3 eyeDir, Material mat)
{
    const float m = mat.Shininess * 256.0f;
    float3 halfVec = normalize(lightDir + eyeDir);

    // S(theta) = (m + 8) / 8 * cosTheta ^ m;
    float ks = (m + 8.0f) * pow(max(dot(halfVec, norm), 0.0f), m) / 8.0f;

#ifdef TOON_SHADING
	ks = ToonShadingKs(ks);
#endif

    float3 fresnel = SchlickFresnel(mat.FresnelR0, halfVec, lightDir);
    float3 specular = fresnel * ks;
    specular = specular / (1.0f + specular);

    return (mat.DiffuseAlbedo.rgb + specular) * intensity;
}

float3 ComputeDirectionalLight(Light light, Material mat, float3 norm, float3 eyeDir)
{
    float3 lightDir = -light.Direction;
    float kd = max(dot(lightDir, norm), 0.0f);

#ifdef TOON_SHADING
    kd = ToonShadingKd(kd);
#endif

    float3 intensity = light.Intensity * kd;
    return BlinnPhong(intensity, lightDir, norm, eyeDir, mat);
}

float3 ComputePointLight(Light light, Material mat, float3 pos, float3 norm, float3 eyeDir)
{
    float3 lightDir = light.Position - pos;
    float dis = length(lightDir);
    if (dis > light.AttnEnd)
        return 0.0f;

    lightDir /= dis;
    float kd = max(dot(lightDir, norm), 0.0f);

#ifdef TOON_SHADING
    kd = ToonShadingKd(kd);
#endif

    float3 intensity = light.Intensity * kd;

    intensity *= CalAttenuation(dis, light.AttnStart, light.AttnEnd);
    return BlinnPhong(intensity, lightDir, norm, eyeDir, mat);
}

float3 ComputeSpotLight(Light light, Material mat, float3 pos, float3 norm, float3 eyeDir)
{
    float3 lightDir = light.Position - pos;
    float dis = length(lightDir);
    if (dis > light.AttnEnd)
        return 0.0f;

    lightDir /= dis;
    float kd = max(dot(lightDir, norm), 0.0f);

#ifdef TOON_SHADING
    kd = ToonShadingKd(kd);
#endif

    float3 intensity = light.Intensity * kd;

    intensity *= CalAttenuation(dis, light.AttnStart, light.AttnEnd);

    intensity *= pow(max(dot(-lightDir, light.Direction), 0.0f), light.SpotPower);

    return BlinnPhong(intensity, lightDir, norm, eyeDir, mat);
}

float4 ComputeLighting(Light lights[LIGHT_MAX], Material mat,
						float3 pos, float3 norm, float3 eyeDir, float3 shadowFactor)
{
    float3 result = 0.0f;
    int i = 0;
    
#if (NUM_DIR_LIGHTS > 0)

	for (i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        result += shadowFactor[i] * ComputeDirectionalLight(lights[i], mat, norm, eyeDir);
    }
#endif

#if (NUM_POINT_LIGHTS > 0)

	for (i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i)
    {
        result += ComputePointLight(lights[i], mat, pos, norm, eyeDir);
    }
#endif

#if (NUM_SPOT_LIGHTS > 0)

	for (i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        result += ComputeSpotLight(lights[i], mat, pos, norm, eyeDir);
    }
#endif

    return float4(result, 0.0f);
}
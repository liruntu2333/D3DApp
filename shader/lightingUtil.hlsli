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
    if (dot(lightDir, norm) <= 0.f)
	{
        ks = 0.0f;
    }
    else if (ks <= 0.0f)
    {
        ks = 0.4f;
    }
    else if (ks <= 0.5f)
    {
        ks = 0.5f;
    }
    else if (ks <= 1.0f)
    {
        ks = 0.8f;
    }
#endif

    float3 fresnel = SchlickFresnel(mat.FresnelR0, halfVec, lightDir);
    float3 specular = fresnel * ks;
    specular = specular / (1.0f + specular);

    return (mat.DiffuseAlbedo.rgb + specular) * intensity;
}

float3 ComputeDirectionalLight(Light light, Material mat, float3 norm, float3 eyeDir)
{
    float3 lightDir = -light.Direction;
    float cosTheta = max(dot(lightDir, norm), 0.0f);


#ifdef TOON_SHADING
    if (cosTheta == 0.0f)
    {
        cosTheta = 0.4f;
    }
    else if (cosTheta <= 0.5)
    {
        cosTheta = 0.6f;
    }
    else
    {
        cosTheta = 1.0f;
    }
#endif

    float3 intensity = light.Intensity * cosTheta;
    return BlinnPhong(intensity, lightDir, norm, eyeDir, mat);
}

float3 ComputePointLight(Light light, Material mat, float3 pos, float3 norm, float3 eyeDir)
{
    float3 lightDir = light.Position - pos;
    float dis = length(lightDir);
    if (dis > light.AttnEnd)
        return 0.0f;

    lightDir /= dis;
    float cosTheta = max(dot(lightDir, norm), 0.0f);

#ifdef TOON_SHADING
    if (cosTheta == 0.0f)
    {
        cosTheta = 0.4f;
    }
    else if (cosTheta <= 0.5)
    {
        cosTheta = 0.6f;
    }
    else
    {
        cosTheta = 1.0f;
    }
#endif

    float3 intensity = light.Intensity * cosTheta;

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
    float cosTheta = max(dot(lightDir, norm), 0.0f);

#ifdef TOON_SHADING
    if (cosTheta == 0.0f)
    {
        cosTheta = 0.4f;
    }
    else if (cosTheta <= 0.5)
    {
        cosTheta = 0.6f;
    }
    else
    {
        cosTheta = 1.0f;
    }
#endif

    float3 intensity = light.Intensity * cosTheta;

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
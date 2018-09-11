#define MaxLights 16

static const float PI = 3.14159265f;
static const float INVPI = 1.0f / PI;

struct Light
{
    float3 Strength;
    float FalloffStart; // point/spot light only
    float3 Direction;   // directional/spot light only
    float FalloffEnd;   // point/spot light only
    float3 Position;    // point light only
    float SpotPower;    // spot light only
};

struct Material
{
    float3 Albedo;  //Fresnel/Diffuse
    float Roughness;
    float Metalness;
};

float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    // Linear falloff.
    return saturate((falloffEnd-d) / (falloffEnd - falloffStart));
}

// Schlick gives an approximation to Fresnel reflectance (see pg. 233 "Real-Time Rendering 3rd Ed.").
// R0 = ( (n-1)/(n+1) )^2, where n is the index of refraction.
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));

    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0)*(f0*f0*f0*f0*f0);

    return reflectPercent;
}

// Add two fresnel factor to build a more accurate diffue model.
float DisneyDiffuse(float3 lightVec, float3 normal, float3 toEye, float roughness)
{
    float oneMinusCosL = 1.0f - abs(dot(normal, lightVec));
    float oneMinusCosLSqr = oneMinusCosL * oneMinusCosL;
    float oneMinusCosV = 1.0f - abs(dot(normal, toEye));
    float oneMinusCosVSqr = oneMinusCosV * oneMinusCosV;

    float IDotH = dot(lightVec, normalize(toEye + lightVec));
    float F_D90 = 0.5f + 2.0f * IDotH * IDotH * roughness;

    return (1.0f + (F_D90 - 1.0f) * oneMinusCosLSqr * oneMinusCosLSqr * oneMinusCosL)
           * (1.0f + (F_D90 - 1.0f) * oneMinusCosVSqr * oneMinusCosVSqr * oneMinusCosV);
}

// Calculate the microsurface distribution based on GGX.
float GGX_D(float3 halfVec, float3 normal, float roughness)
{
    float cosTheta = max(dot(halfVec, normal), 0.0f);
    float cosTheta2 = cosTheta * cosTheta;
    [flatten]
    if (cosTheta2 < 0.00001f)
        return 0.0f;
    
    float tanTheta2 = (1.0f - cosTheta2) / cosTheta2;
    float root = roughness / (cosTheta2 * (roughness * roughness + tanTheta2));
    return INVPI * root * root;
}

// Smith G1 item for micosurface occlusion.
float Smith_G1(float3 vec, float3 halfVec, float3 normal, float roughness)
{
    float cosTheta = dot(vec, normal);
    [flatten]
    if (cosTheta > 0.9999f)
        return 1.0f;
    [flatten]
    if (dot(vec, halfVec) * cosTheta <= 0.00001f)
        return 0.0f;

    float tanTheta = abs(sqrt(1.0f - cosTheta * cosTheta) / cosTheta);
    float root = roughness * tanTheta;
    return 2.0f / (1.0f + sqrt(1.0f + root * root));
}

// Smith G item for microsurface occlusion.
float Smith_G(float3 toEye, float3 lightVec, float3 halfVec, float3 normal, float roughness)
{
    return Smith_G1(toEye, halfVec, normal, roughness) * Smith_G1(lightVec, halfVec, normal, roughness);
}

// Use Cook Torrance model to calculate light.
float3 CookTorrance(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    float3 diffuse = mat.Albedo * (1.0f - mat.Metalness);
    float3 specular = lerp(float3(0.04f, 0.04f, 0.04f), mat.Albedo, float3(mat.Metalness, mat.Metalness, mat.Metalness));

    float3 halfVec = normalize(toEye + lightVec);
    float roughness = mat.Roughness;
    float D = GGX_D(halfVec, normal, roughness);
    float G = Smith_G(toEye, lightVec, halfVec, normal, roughness);
    float3 F = SchlickFresnel(specular, halfVec, lightVec);

    float3 specAlbedo = D * G * F;
    float cos = max(dot(lightVec, normal), 0.0f) * max(dot(toEye, normal), 0.0f);
    [flatten]
    if (cos <= 0.0f)
        specAlbedo = float3(0.0f, 0.0f, 0.0f);
    else
        specAlbedo = specAlbedo / cos * PI / 4.0f;

    float disney = DisneyDiffuse(lightVec, normal, toEye, roughness);

    return (diffuse * disney + specAlbedo) * lightStrength;
}

// Use BlinnPhong model to calculate light.
float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    float3 diffuse = mat.Albedo * (1.0f - mat.Metalness);
    float3 specular = lerp(float3(0.04f, 0.04f, 0.04f), mat.Albedo, float3(mat.Metalness, mat.Metalness, mat.Metalness));

    const float m = (1.0f - mat.Roughness) * 256.0f;
    float3 halfVec = normalize(toEye + lightVec);

    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(specular, halfVec, lightVec);

    float3 specAlbedo = fresnelFactor * roughnessFactor;

    // Our spec formula goes outside [0,1] range, but we are 
    // doing LDR rendering.  So scale it down a bit.
    specAlbedo = specAlbedo / (specAlbedo + 1.0f);

    return (diffuse + specAlbedo) * lightStrength;
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for directional lights.
//---------------------------------------------------------------------------------------
float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 toEye)
{
    // The light vector aims opposite the direction the light rays travel.
    float3 lightVec = normalize(-L.Direction);

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    return CookTorrance(lightStrength, lightVec, normal, toEye, mat);
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for point lights.
//---------------------------------------------------------------------------------------
float3 ComputePointLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // The vector from the surface to the light.
    float3 lightVec = L.Position - pos;

    // The distance from surface to light.
    float d = length(lightVec);

    // Range test.
    if(d > L.FalloffEnd)
        return 0.0f;

    // Normalize the light vector.
    lightVec /= d;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    // Attenuate light by distance.
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    return CookTorrance(lightStrength, lightVec, normal, toEye, mat);
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for spot lights.
//---------------------------------------------------------------------------------------
float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // The vector from the surface to the light.
    float3 lightVec = L.Position - pos;

    // The distance from surface to light.
    float d = length(lightVec);

    // Range test.
    if(d > L.FalloffEnd)
        return 0.0f;

    // Normalize the light vector.
    lightVec /= d;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    // Attenuate light by distance.
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    // Scale by spotlight
    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.0f), L.SpotPower);
    lightStrength *= spotFactor;

    return CookTorrance(lightStrength, lightVec, normal, toEye, mat);
}

float4 ComputeLighting(Light gLights[MaxLights], Material mat,
                       float3 pos, float3 normal, float3 toEye,
                       float3 shadowFactor)
{
    float3 result = 0.0f;

    int i = 0;

#if (NUM_DIR_LIGHTS > 0)
    for(i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        result += shadowFactor[i] * ComputeDirectionalLight(gLights[i], mat, normal, toEye);
    }
#endif

#if (NUM_POINT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS+NUM_POINT_LIGHTS; ++i)
    {
        result += ComputePointLight(gLights[i], mat, pos, normal, toEye);
    }
#endif

#if (NUM_SPOT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        result += ComputeSpotLight(gLights[i], mat, pos, normal, toEye);
    }
#endif 

    return float4(result, 1.0f);
}



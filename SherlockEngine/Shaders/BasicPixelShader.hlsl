#include "Common.hlsli"

float CalculateDistanceAttenuation(LightData light, float distanceToLight)
{
    const float denominator = max(
        light.attenuation.x +
        light.attenuation.y * distanceToLight +
        light.attenuation.z * distanceToLight * distanceToLight,
        EPSILON);

    const float safeRange = max(light.range, EPSILON);
    const float rangeFactor = saturate(1.0f - distanceToLight / safeRange);
    return (rangeFactor * rangeFactor) / denominator;
}

float4 PS_Main(VSOutput input) : SV_TARGET
{
    const float3 albedo = input.col.rgb;
    const float3 normal = SafeNormalize(input.normal, float3(0.0f, 1.0f, 0.0f));
    const float3 viewDirection = SafeNormalize(
        cameraPosition - input.worldPosition, float3(0.0f, 0.0f, -1.0f));

    float3 finalColor = albedo * ambientColor;
    const uint activeLightCount = min(lightCount, MAX_LIGHTS);

    [loop]
    for (uint i = 0; i < activeLightCount; ++i)
    {
        const LightData light = lights[i];
        float3 surfaceToLight = float3(0.0f, 1.0f, 0.0f);
        float attenuation = 1.0f;

        if (light.type == LIGHT_DIRECTIONAL)
        {
            surfaceToLight = SafeNormalize(-light.direction, float3(0.0f, 1.0f, 0.0f));
        }
        else
        {
            const float3 toLight = light.position - input.worldPosition;
            const float distanceToLight = length(toLight);
            surfaceToLight = SafeNormalize(toLight, float3(0.0f, 1.0f, 0.0f));
            attenuation = CalculateDistanceAttenuation(light, distanceToLight);

            if (light.type == LIGHT_SPOT)
            {
                const float3 lightDirection = SafeNormalize(
                    light.direction, float3(0.0f, -1.0f, 0.0f));
                const float3 lightToSurface = -surfaceToLight;
                const float coneCos = dot(lightDirection, lightToSurface);
                const float coneWidth = max(light.innerConeCos - light.outerConeCos, EPSILON);
                attenuation *= saturate((coneCos - light.outerConeCos) / coneWidth);
            }
        }

        const float nDotL = saturate(dot(normal, surfaceToLight));
        const float3 halfVector = SafeNormalize(
            surfaceToLight + viewDirection, normal);
        const float specularAmount = (nDotL > 0.0f)
            ? pow(saturate(dot(normal, halfVector)), max(shininess, 1.0f))
            : 0.0f;

        const float3 radiance = light.color * max(light.intensity, 0.0f) * attenuation;
        finalColor += (albedo * nDotL + specularColor * specularAmount) * radiance;
    }

    return float4(finalColor, input.col.a);
}

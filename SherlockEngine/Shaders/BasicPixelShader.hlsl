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

// 6단계: 그림자 맵 비교. 광원 클립 좌표 → 텍스처 UV → 3×3 PCF.
// 1 = 완전히 빛을 받음, 0 = 완전히 그림자. 맵 밖은 빛을 받는 것으로 본다(Border 1).
float ComputeShadowFactor(float4 shadowPosition)
{
    if (shadowParams.w < 0.5f)
    {
        return 1.0f;
    }
    // 직교 투영이라 w = 1 이지만 관례대로 나눈다.
    const float3 p = shadowPosition.xyz / shadowPosition.w;
    // NDC x,y ∈ [−1, 1] → UV. D3D 텍스처 공간은 v 가 아래로 자라므로 y 부호를 뒤집는다.
    const float2 uv = float2(p.x * 0.5f + 0.5f, -p.y * 0.5f + 0.5f);
    if (p.z > 1.0f)
    {
        return 1.0f;   // 광원 far 밖
    }
    const float depth = p.z - shadowParams.y;   // 수신자 깊이에서 바이어스를 뺀다(자기 그림자 방지)

    // PCF: 이웃 텍셀 9개의 비교 결과(0/1)를 평균 낸다. SampleCmp 는 비교 후 보간까지 하드웨어가 한다.
    float lit = 0.0f;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            lit += shadowMap.SampleCmpLevelZero(shadowSampler, uv, depth, int2(dx, dy));
        }
    }
    lit /= 9.0f;
    return lerp(1.0f, lit, shadowParams.z);
}

float4 PS_Main(VSOutput input) : SV_TARGET
{
    // 재질의 기본색 × 정점 색 × 텍스처. sRGB 텍스처는 샘플 시 하드웨어가 선형으로 풀어 주므로
    // 여기서부터 모든 색은 선형 공간이다. 백버퍼의 sRGB 뷰가 쓸 때 다시 인코딩한다.
    const float4 texel = albedoTexture.Sample(albedoSampler, input.uv);
    const float4 surface = input.col * baseColor * texel;
    const float3 albedo = surface.rgb;

    // 9단계: 알파 컷아웃 (glTF MASK). 나뭇잎·격자처럼 알파로 구멍을 낸 재질.
    if (alphaCutoff > 0.0f)
    {
        clip(surface.a - alphaCutoff);
    }

    // 감마 검증용: 조명 없이 알베도를 그대로 낸다. 128 회색 텍스처가 화면에서 128로 읽혀야 한다.
    if (unlit > 0.5f)
    {
        return float4(albedo, surface.a);
    }

    // 6단계: 노멀 매핑. 탄젠트 공간(T, B, N) 의 노멀 맵 값을 월드 공간으로 되돌린다.
    // 노멀 맵은 선형 텍스처이고 (0.5, 0.5, 1) 이 "평평". B 는 정점의 손잡이(w)로 복원한다.
    const float3 geometricNormal = SafeNormalize(input.normal, float3(0.0f, 1.0f, 0.0f));
    const float3 T = SafeNormalize(input.tangent.xyz - geometricNormal * dot(geometricNormal, input.tangent.xyz), float3(1.0f, 0.0f, 0.0f));
    const float3 B = cross(geometricNormal, T) * input.tangent.w;
    float3 tangentNormal = normalTexture.Sample(albedoSampler, input.uv).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= normalStrength;
    const float3 normal = SafeNormalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * geometricNormal, geometricNormal);

    const float3 viewDirection = SafeNormalize(
        cameraPosition - input.worldPosition, float3(0.0f, 0.0f, -1.0f));

    const float shadowFactor = ComputeShadowFactor(input.shadowPosition);

    // 11단계: 디버그 뷰. 에디터의 Render Settings 에서 고른다.
    const int debugView = (int)debugParams.x;
    if (debugView == 1) return float4(albedo, 1.0f);
    if (debugView == 2) return float4(normal * 0.5f + 0.5f, 1.0f);
    if (debugView == 3) { const float d = saturate(length(cameraPosition - input.worldPosition) / max(debugParams.y, 0.001f)); return float4(d, d, d, 1.0f); }
    if (debugView == 4) return float4(shadowFactor, shadowFactor, shadowFactor, 1.0f);
    if (debugView == 5) return float4(frac(input.uv), 0.0f, 1.0f);

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
            // 그림자 맵은 방향광 0 하나에 대해서만 만든다.
            if (i == 0)
            {
                attenuation *= shadowFactor;
            }
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

    return float4(finalColor, surface.a);
}

// 표면 하나의 조명 계산 (포워드 BasicPS · 디퍼드 라이팅 PS 공용).
//
// 재질에서 나온 값(SurfaceData)과 월드 위치·시선 방향만 받아 환경광(IBL) + 직접광(조명 루프) + 그림자를 계산함.
// "재질을 어떻게 읽었는가"는 모름 — 포워드는 재질 텍스처에서 (MaterialSurface.hlsli), 디퍼드는 G-buffer 에서
// (GBuffer.hlsli) SurfaceData 를 채워 같은 함수를 부름. 두 경로의 화면이 같아야 한다는 검증이 이 공유에 기댐.
//
// 조명 목록은 두 가지:
//   ShadeAllLights        t15 의 조명 전부 (lightCount 개) — 정답 경로
//   ShadeClusteredLights  방향광(앞쪽 directionalCount 개) + 이 픽셀 클러스터의 지역광 (t30, t31) — 3단계
// 프레임 셋(Lighting.hlsli)만 읽음. 재질 셋·G-buffer 셋은 모름.
#ifndef SHERLOCK_SHADING_HLSLI
#define SHERLOCK_SHADING_HLSLI

#include "Lighting.hlsli"
#include "Clusters.hlsli"

// 재질 입력을 조명에 필요한 형태로 푼 것. 모든 벡터는 월드 공간, 색은 선형.
struct SurfaceData
{
    float3 albedo;            // 기본색 (텍스처 × 정점 색 × baseColor). 투과를 반영하기 전
    float alpha;              // 기본색 알파 (블렌딩 재질의 불투명도)
    float3 F0;                // 수직 반사율 (유전체 = 굴절률·반사 색·세기, 금속 = albedo)
    float F90;                // 스치는 각도의 반사율 (유전체 = specularFactor, 금속 = 1)
    float metallic;
    float roughness;          // 0.03 .. 1 (지각적 거칠기. GGX 의 α = roughness²)
    float ao;                 // 재질 AO (occlusionStrength 반영). SSAO 는 호출 쪽이 따로 곱함
    float transmission;       // 0 .. 1. 확산을 이만큼 줄이고 블렌딩 재질은 알파도 줄임 (디퍼드는 항상 0)
    float3 normal;            // 노말맵을 적용한 셰이딩 노멀
    float3 geometricNormal;   // 삼각형(정점 보간) 노멀. 뒷면이면 뒤집힌 것. 그림자 오프셋에 씀
    float3 emission;
    float clearcoat;          // 0 이면 코팅 없음
    float clearcoatRoughness;
    float3 clearcoatNormal;   // 코팅 노멀 맵이 없으면 geometricNormal 과 같음
};

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

// 그림자 뷰 하나에 대한 PCF 5×5. 1 = 빛을 받음, 0 = 그림자.
// 뷰 밖이면 빛을 받는 것으로 간주함 (예전의 Border 1 을 대신함 — 아틀라스는 옆 타일이 있어 Border 를 못 씀).
// normalDelta = (노말맵 적용 노멀) − (기하 노멀). 그림자 경계가 노말맵의 영향을 받도록 UV 를 그만큼 옮김.
// distanceToLight: 원근 뷰의 텍셀 월드 크기를 구할 때 씀 (직교 뷰는 무시).
// coverage: 이 위치가 뷰 안이면 1, 밖이면 0. 캐스케이드 블렌딩이 "다음 캐스케이드가 이 위치를 덮는지" 확인하는 데 씀.
float SampleShadowViewCoverage(uint viewIndex, float3 worldPosition, float3 geometricNormal, float3 normalDelta, float distanceToLight, out float coverage)
{
    // 단일 반환 구조: 조기 반환이 섞이면 fxc 가 -Od(디버그) 에서 X4000 오경고를 냄.
    float result = 1.0f;
    const ShadowViewData view = shadowViews[viewIndex];
    const bool perspective = view.params.z > 0.5f;

    // (1) 원근 뷰만: 받는 쪽 위치를 기하 노멀 방향으로 "텍셀 몇 개"만큼 밀어 자기 그림자를 피함
    const float texelWorld = perspective ? view.params.y * distanceToLight : view.params.y;
    const float3 p = worldPosition + geometricNormal * (perspective ? shadowAtlasParams.z * texelWorld : 0.0f);

    // (2) 월드 → 이 뷰의 클립 → NDC → 타일 안 UV (0..1). D3D 텍스처 공간은 v 가 아래쪽으로 증가하므로 y 부호를 뒤집음.
    const float4 clip = mul(float4(p, 1.0f), view.viewProj);
    const float3 ndc = clip.xyz / clip.w;
    float2 local = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);

    // (3) 노말 섀도: 노멀 차이를 같은 행렬의 방향(w = 0)으로 옮겨 UV 에 더함. shadowExtra.x(에디터의 Normal
    // offset)로 조절함.
    //     직교면 w = 1 이라 예전 계산과 같음.
    const float4 deltaClip = mul(float4(normalDelta, 0.0f), view.viewProj);
    local += deltaClip.xy / clip.w * float2(0.5f, -0.5f) * shadowExtra.x;

    // (4) 이 뷰 밖이면 result = 1 그대로. 원근 뷰는 광원 뒤(w ≤ 0)와 near 앞(z < 0)도 밖.
    const bool inside = clip.w > 0.0f && all(local >= 0.0f) && all(local <= 1.0f) && ndc.z <= 1.0f && (!perspective || ndc.z >= 0.0f);
    coverage = inside ? 1.0f : 0.0f;
    if (shadowParams.w >= 0.5f && inside)
    {
        // (5) 타일 UV → 아틀라스 UV. PCF 커널이 옆 타일을 읽지 않도록 여백(텍셀)만큼 안쪽으로 clamp.
        //     params.w == 1 이면 CSM 전용 아틀라스(t14, 가로·세로가 다를 수 있음), 아니면 메인 아틀라스(t8).
        const bool csm = view.params.w > 0.5f;
        float2 uv = view.atlasRect.xy + local * view.atlasRect.zw;
        const float2 border = shadowAtlasParams.y * (csm ? csmAtlasParams.xy : shadowAtlasParams.xx);
        uv = clamp(uv, view.atlasRect.xy + border, view.atlasRect.xy + view.atlasRect.zw - border);
        const float depth = ndc.z - view.params.x;   // 수신자 깊이에서 바이어스를 뺌(자기 그림자 방지)

        // (6) PCF: 이웃 텍셀 25개의 비교 결과(0/1)를 평균 냄. SampleCmp 는 비교 후 보간까지 하드웨어에서 처리함.
        //     텍스처는 변수로 고를 수 없어서 아틀라스마다 루프를 따로 둠. 분기 조건은 뷰 단위라 한 드로우
        //     안에서 균일함.
        float lit = 0.0f;
        if (csm)
        {
            [unroll]
            for (int cy = -2; cy <= 2; ++cy)
            {
                [unroll]
                for (int cx = -2; cx <= 2; ++cx)
                {
                    lit += csmMap.SampleCmpLevelZero(shadowSampler, uv, depth, int2(cx, cy));
                }
            }
        }
        else
        {
            [unroll]
            for (int dy = -2; dy <= 2; ++dy)
            {
                [unroll]
                for (int dx = -2; dx <= 2; ++dx)
                {
                    lit += shadowMap.SampleCmpLevelZero(shadowSampler, uv, depth, int2(dx, dy));
                }
            }
        }
        result = lerp(1.0f, lit / 25.0f, shadowParams.z);
    }
    return result;
}

float SampleShadowView(uint viewIndex, float3 worldPosition, float3 geometricNormal, float3 normalDelta, float distanceToLight)
{
    float coverage;
    return SampleShadowViewCoverage(viewIndex, worldPosition, geometricNormal, normalDelta, distanceToLight, coverage);
}

// 광원 → 표면 벡터의 가장 큰 성분 축이 그 면. 순서는 +X, −X, +Y, −Y, +Z, −Z (C++ 의 face 번호와 같음)
uint PointFace(float3 v)
{
    const float3 a = abs(v);
    uint face = (v.z > 0.0f) ? 4 : 5;
    if (a.x >= a.y && a.x >= a.z) face = (v.x > 0.0f) ? 0 : 1;
    else if (a.y >= a.z)           face = (v.y > 0.0f) ? 2 : 3;
    return face;
}

// CSM 이 켜져 있으면 이 위치의 캐스케이드 번호, 꺼져 있거나 마지막 캐스케이드 너머면 −1.
// 깊이를 지금 그리는 카메라의 b0 가 아니라 cascadeOrigin/Forward(캐스케이드를 계산한 카메라)로 잼:
// 카메라 미리보기 패스는 b0 를 다른 카메라로 바꾸지만, 그림자는 메인 카메라 기준으로 한 번만 만들기 때문임.
int SelectCascade(float3 worldPosition)
{
    int result = -1;
    const uint count = (uint)cascadeOrigin.w;
    const float depth = dot(worldPosition - cascadeOrigin.xyz, cascadeForward.xyz);
    // 캐스케이드 카메라 뒤쪽(depth < 0)은 어느 구간도 아님. 카메라를 고정하고 바깥에서 볼 때만 나타나는 위치라
    // 이 검사가 없으면 첫 캐스케이드로 잘못 분류되어 타일 밖을 샘플링함.
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        if (result < 0 && depth >= 0.0f && i < count && depth < cascadeSplits[i]) result = (int)i;
    }
    return result;
}

// 조명 하나의 그림자 계수. 그림자가 없는 조명은 1.
float ShadowForLight(LightData light, float3 worldPosition, float3 geometricNormal, float3 normalDelta, float distanceToLight)
{
    float result = 1.0f;
    if (light.shadowIndex >= 0)
    {
        if (light.type == LIGHT_DIRECTIONAL)
        {
            if (cascadeOrigin.w > 0.5f)
            {
                // CSM: 마지막 캐스케이드 너머(−1)는 그림자 없음
                const int c = SelectCascade(worldPosition);
                if (c >= 0)
                {
                    float coverage;
                    result = SampleShadowViewCoverage((uint)(light.shadowIndex + c), worldPosition, geometricNormal, normalDelta, 0.0f, coverage);

                    // 경계 블렌딩: 캐스케이드 c 의 끝 쪽 band 구간에서 t 가 0 → 1 로 올라감. band = 그
                    // 캐스케이드 깊이 길이 × 비율.
                    const uint count = (uint)cascadeOrigin.w;
                    const float blend = cascadeForward.w;
                    if (blend > 0.0f)
                    {
                        const float depth = dot(worldPosition - cascadeOrigin.xyz, cascadeForward.xyz);
                        const float end = cascadeSplits[c];
                        const float begin = (c > 0) ? cascadeSplits[c - 1] : 0.0f;
                        const float band = max((end - begin) * blend, EPSILON);
                        const float t = saturate((depth - (end - band)) / band);
                        if (t > 0.0f)
                        {
                            if ((uint)c + 1 < count)
                            {
                                // 다음 캐스케이드도 읽어 섞음. 다음 뷰의 구 바깥이면(coverage 0) 섞지 않음 —
                                // 밖은 "빛 받음"이라 그림자가 거짓으로 옅어짐.
                                float nextCoverage;
                                const float next = SampleShadowViewCoverage((uint)(light.shadowIndex + c + 1), worldPosition, geometricNormal, normalDelta, 0.0f, nextCoverage);
                                if (nextCoverage > 0.5f) result = lerp(result, next, t);
                            }
                            else
                            {
                                result = lerp(result, 1.0f, t);   // 마지막 캐스케이드: 끝에서 그림자가 갑자기 끊기지 않고 서서히 사라짐
                            }
                        }
                    }
                }
            }
            else
            {
                result = SampleShadowView((uint)light.shadowIndex, worldPosition, geometricNormal, normalDelta, 0.0f);
            }
        }
        else if (light.type == LIGHT_SPOT)
        {
            result = SampleShadowView((uint)light.shadowIndex, worldPosition, geometricNormal, normalDelta, distanceToLight);
        }
        else if (light.type == LIGHT_POINT)
        {
            const uint face = PointFace(worldPosition - light.position);
            result = SampleShadowView((uint)light.shadowIndex + face, worldPosition, geometricNormal, normalDelta, distanceToLight);
        }
    }
    return result;
}

// ---- PBR (Cook-Torrance, GGX) ----
// 직접광: f = kd · albedo / π + D · G · F / (4 · N·L · N·V).
// 조명 세기 관례: LightData 의 color × intensity 를 "빛에 수직인 면이 받는 조도 E" 로 봄. 그래서 BRDF 에 π 를 곱해
// 확산항이 albedo · E · N·L 이 됨 — Blinn-Phong 시절의 albedo · N·L · radiance 와 같은 밝기라 기존 씬의 조명
// 값을 그대로 씀.
float DistributionGGX(float NdotH, float alpha)
{
    // Trowbridge-Reitz. α = roughness² (Disney/Karis 의 지각적 거칠기 매핑)
    const float alpha2 = alpha * alpha;
    const float d = NdotH * NdotH * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / max(PI * d * d, EPSILON);
}

float GeometrySchlickGGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0f - k) + k);
}

float GeometrySmith(float NdotL, float NdotV, float roughness)
{
    // 직접광용 k = (roughness + 1)² / 8 (Karis 2013). 빛 방향과 시선 방향의 가림을 곱함.
    const float r = roughness + 1.0f;
    const float k = r * r / 8.0f;
    return GeometrySchlickGGX(NdotL, k) * GeometrySchlickGGX(NdotV, k);
}

// F90 = 스치는 각도에서의 반사율. 금속과 기본 재질은 1, 비금속은 KHR_materials_specular 의 specularFactor.
float3 FresnelSchlick(float3 F0, float F90, float cosTheta)
{
    return F0 + (F90 - F0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

// 환경광용. 거칠수록 스치는 각도의 반사가 덜 세지도록 최대값을 (1 − roughness)·F90 으로 누름 (Lagarde/Sébastien).
float3 FresnelSchlickRoughness(float3 F0, float F90, float cosTheta, float roughness)
{
    return F0 + (max(((1.0f - roughness) * F90).xxx, F0) - F0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

// BRDF LUT 가 없을 때의 해석적 근사 (Karis, "Physically Based Shading on Mobile" 2014). 결과는 LUT 의 RG 와 같은 뜻.
float2 EnvBRDFApprox(float roughness, float NdotV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

// 환경광(IBL, split-sum). 확산 = (1 − F) · (1 − metallic) · albedo · 확산 조도(N),
// 반사 = 프리필터(R, 거칠기 밉) · (F0 · A + F90 · B), (A, B) = BRDF LUT(N·V, 1 − roughness).
// 환경 큐브가 없으면 두 조도를 ambientColor 하나로 대신함 — PBR 이전의 albedo · ambientColor 와 거의 같은 밝기.
float3 EnvironmentLighting(float3 albedo, float3 F0, float F90, float metallic, float roughness, float3 N, float3 V, float ao)
{
    const float NdotV = max(dot(N, V), 0.0f);
    const float3 F = FresnelSchlickRoughness(F0, F90, NdotV, roughness);
    const float3 kd = (1.0f - F) * (1.0f - metallic);

    float3 irradiance = ambientColor;
    float3 prefiltered = ambientColor;
    float intensity = 1.0f;
    if (environmentParams.w > 0.5f)
    {
        irradiance = irradianceMap.SampleLevel(environmentSampler, N, environmentParams.z).rgb * iblTint.rgb;
        const float maxLod = max(environmentParams.y - 1.0f, 0.0f);
        prefiltered = prefilteredMap.SampleLevel(environmentSampler, reflect(-V, N), roughness * maxLod).rgb * iblTint.rgb;
        intensity = environmentParams.x;
    }
    const float2 brdf = outputParams.z > 0.5f
        ? brdfLut.SampleLevel(environmentSampler, float2(NdotV, 1.0f - roughness), 0.0f).rg
        : EnvBRDFApprox(roughness, NdotV);

    return (kd * albedo * irradiance + prefiltered * (F0 * brdf.x + F90 * brdf.y)) * intensity * ao;
}

// 클리어코트의 환경 반사. 코팅은 F0 0.04 의 유전체 층이라 기본 층의 IBL 과 같은 split-sum 을 코팅 노멀·코팅 거칠기로 한 번 더
// 계산함. fresnel 은 기본 층이 코팅 아래에서 줄어드는 비율(1 − clearcoat · fresnel)을 호출 쪽이 쓰도록 돌려줌.
float3 EnvironmentClearcoat(float3 N, float3 V, float roughness, float ao, out float fresnel)
{
    const float NdotV = max(dot(N, V), 0.0f);
    fresnel = 0.04f + 0.96f * pow(1.0f - saturate(NdotV), 5.0f);

    float3 prefiltered = ambientColor;
    float intensity = 1.0f;
    if (environmentParams.w > 0.5f)
    {
        const float maxLod = max(environmentParams.y - 1.0f, 0.0f);
        prefiltered = prefilteredMap.SampleLevel(environmentSampler, reflect(-V, N), roughness * maxLod).rgb * iblTint.rgb;
        intensity = environmentParams.x;
    }
    const float2 brdf = outputParams.z > 0.5f
        ? brdfLut.SampleLevel(environmentSampler, float2(NdotV, 1.0f - roughness), 0.0f).rg
        : EnvBRDFApprox(roughness, NdotV);
    return prefiltered * (0.04f * brdf.x + brdf.y) * intensity * ao;
}

// 확산에 쓰는 albedo. 투과는 비금속의 확산을 transmission 만큼 줄임 (반사는 줄이지 않음).
float3 DiffuseAlbedo(SurfaceData s)
{
    return s.albedo * (1.0f - s.transmission);
}

// 환경광 전체 (기본 층 IBL + 클리어코트 IBL). ao = 재질 AO 에 SSAO 를 합친 값 (호출 쪽이 정함). 발광은 넣지 않음.
float3 ShadeEnvironment(SurfaceData s, float3 viewDirection, float ao)
{
    float3 environment = EnvironmentLighting(DiffuseAlbedo(s), s.F0, s.F90, s.metallic, s.roughness, s.normal, viewDirection, ao);
    if (s.clearcoat > 0.0f)
    {
        float coatFresnel;
        const float3 coatSpecular = EnvironmentClearcoat(s.clearcoatNormal, viewDirection, s.clearcoatRoughness, ao, coatFresnel);
        environment = environment * (1.0f - s.clearcoat * coatFresnel) + s.clearcoat * coatSpecular;
    }
    return environment;
}

// 조명 하나의 직접광 (그림자 포함). 빛이 닿지 않으면 0.
// NdotV = max(N·V, EPSILON) — 조명 루프 밖에서 한 번 구해 넘김.
float3 ShadeLight(SurfaceData s, LightData light, float3 worldPosition, float3 viewDirection, float NdotV)
{
    float3 result = float3(0.0f, 0.0f, 0.0f);
    float3 surfaceToLight = float3(0.0f, 1.0f, 0.0f);
    float attenuation = 1.0f;
    float distanceToLight = 0.0f;   // 점·스팟만 채움. 그림자의 원근 뷰가 텍셀 크기를 구할 때 씀

    if (light.type == LIGHT_DIRECTIONAL)
    {
        surfaceToLight = SafeNormalize(-light.direction, float3(0.0f, 1.0f, 0.0f));
    }
    else
    {
        const float3 toLight = light.position - worldPosition;
        distanceToLight = length(toLight);
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

    const float NdotL = saturate(dot(s.normal, surfaceToLight));
    const float NdotLcc = s.clearcoat > 0.0f ? saturate(dot(s.clearcoatNormal, surfaceToLight)) : 0.0f;
    if (NdotL > 0.0f || NdotLcc > 0.0f)
    {
        // 빛이 닿지 않는 픽셀(원뿔 밖·범위 밖)은 PCF 를 건너뜀
        if (attenuation > 0.0f)
        {
            attenuation *= ShadowForLight(light, worldPosition, s.geometricNormal, s.normal - s.geometricNormal, distanceToLight);
        }
        const float3 halfVector = SafeNormalize(surfaceToLight + viewDirection, s.normal);
        const float NdotH = saturate(dot(s.normal, halfVector));
        const float VdotH = saturate(dot(viewDirection, halfVector));

        // 조도 관례(위 주석): BRDF 전체에 π 를 곱하므로 확산항은 kd · albedo, 반사항은 specular · π 가 됨.
        const float3 radiance = light.color * max(light.intensity, 0.0f) * attenuation;
        float3 baseTerm = float3(0.0f, 0.0f, 0.0f);
        if (NdotL > 0.0f)
        {
            const float D = DistributionGGX(NdotH, s.roughness * s.roughness);
            const float G = GeometrySmith(NdotL, NdotV, s.roughness);
            const float3 F = FresnelSchlick(s.F0, s.F90, VdotH);
            const float3 specular = D * G * F / max(4.0f * NdotL * NdotV, EPSILON);
            const float3 kd = (1.0f - F) * (1.0f - s.metallic);   // 반사된 몫과 금속을 뺀 나머지만 확산함 (에너지 보존)
            baseTerm = (kd * DiffuseAlbedo(s) + specular * PI) * radiance * NdotL;
        }

        // 클리어코트: F0 0.04 의 GGX 로브 하나를 코팅 노멀로 더하고, 기본 층은 코팅이 반사한 만큼(1 − clearcoat·Fc) 줄임.
        // 하프벡터는 노멀과 무관해서 위의 것을 그대로 씀.
        if (s.clearcoat > 0.0f)
        {
            const float coatFresnel = s.clearcoat * (0.04f + 0.96f * pow(1.0f - VdotH, 5.0f));
            float3 coatTerm = float3(0.0f, 0.0f, 0.0f);
            if (NdotLcc > 0.0f)
            {
                const float NdotVcc = max(dot(s.clearcoatNormal, viewDirection), EPSILON);
                const float NdotHcc = saturate(dot(s.clearcoatNormal, halfVector));
                const float Dcc = DistributionGGX(NdotHcc, s.clearcoatRoughness * s.clearcoatRoughness);
                const float Gcc = GeometrySmith(NdotLcc, NdotVcc, s.clearcoatRoughness);
                coatTerm = (Dcc * Gcc * coatFresnel / max(4.0f * NdotLcc * NdotVcc, EPSILON) * PI).xxx * radiance * NdotLcc;
            }
            result = baseTerm * (1.0f - coatFresnel) + coatTerm;
        }
        else
        {
            result = baseTerm;
        }
    }
    return result;
}

// 정답 경로: t15 의 조명 전부. 2단계 디퍼드 라이팅의 기본이고, 클러스터드(3단계)를 끄면 이 함수로 돌아옴.
// color 에 조명을 하나씩 더해 돌려줌 (시작값을 받는 이유: 부동소수점 덧셈은 순서가 바뀌면 최하위 비트가 달라짐 —
// 리팩터 전 BasicPS 의 "환경 + 발광 → 조명 순서대로 +=" 와 같은 순서를 지켜 화면이 비트 단위로 같게 함).
float3 ShadeAllLights(SurfaceData s, float3 worldPosition, float3 viewDirection, float3 color)
{
    const float NdotV = max(dot(s.normal, viewDirection), EPSILON);
    const uint activeLightCount = lightCount;
    [loop]
    for (uint i = 0; i < activeLightCount; ++i)
    {
        color += ShadeLight(s, lights[i], worldPosition, viewDirection, NdotV);
    }
    return color;
}

// 디버그 뷰 (에디터 Render Settings · b0 debugParams.x). 표면 값만으로 그릴 수 있는 것을 여기 모음 — 포워드와
// 디퍼드가 같은 색을 내야 두 경로를 디버그 뷰로도 비교할 수 있음. 해당하는 뷰면 true 와 색을 돌려줌.
// 5 (UV) 는 G-buffer 에 UV 가 없어 포워드 전용 (BasicPS), 13 (SSAO)·14·15 (클러스터)는 호출 쪽이 처리함.
bool SurfaceDebugColor(int debugView, SurfaceData s, float3 worldPosition, out float3 color)
{
    color = float3(0.0f, 0.0f, 0.0f);
    bool handled = true;
    if (debugView == 1) color = s.albedo;
    else if (debugView == 2) color = s.normal * 0.5f + 0.5f;
    else if (debugView == 3) { const float d = saturate(length(cameraPosition - worldPosition) / max(debugParams.y, 0.001f)); color = float3(d, d, d); }
    else if (debugView == 4)
    {
        // 그림자가 있는 모든 조명의 계수를 곱해 보여 줌
        float sh = 1.0f;
        for (uint k = 0; k < lightCount; ++k)
        {
            const LightData l = lights[k];
            const float d = length(l.position - worldPosition);
            sh *= ShadowForLight(l, worldPosition, s.geometricNormal, s.normal - s.geometricNormal, d);
        }
        color = float3(sh, sh, sh);
    }
    else if (debugView == 6) color = float3(0.0f, s.roughness, s.metallic);   // glTF 의 metallicRoughness 텍스처와 같은 채널 배치
    else if (debugView == 7) color = float3(s.ao, s.ao, s.ao);
    else if (debugView == 9) color = s.F0;                                     // 유효 F0 (비금속 4% = 약 0.04, 금속은 기본 색)
    else if (debugView == 10) color = float3(s.clearcoat, s.clearcoatRoughness, 0.0f);   // R = 코팅 세기, G = 코팅 거칠기
    else if (debugView == 8)
    {
        // 캐스케이드: 0 빨강, 1 초록, 2 파랑, 3 노랑, 마지막 너머는 어두운 회색, CSM 꺼짐은 흰색. 장면을
        // 알아보도록 알베도를 조금 섞음.
        float3 tint = float3(1.0f, 1.0f, 1.0f);
        if (cascadeOrigin.w > 0.5f)
        {
            const int c = SelectCascade(worldPosition);
            tint = float3(0.15f, 0.15f, 0.15f);
            if (c == 0) tint = float3(1.0f, 0.1f, 0.1f);
            else if (c == 1) tint = float3(0.1f, 1.0f, 0.1f);
            else if (c == 2) tint = float3(0.1f, 0.1f, 1.0f);
            else if (c == 3) tint = float3(1.0f, 1.0f, 0.1f);
        }
        color = lerp(tint, s.albedo, 0.3f);
    }
    // 11: 월드 위치의 소수부 (1 m 격자 무늬). 디퍼드는 깊이에서 복원한 위치라, 포워드와 무늬가 같으면 복원이 맞음.
    else if (debugView == 11) color = frac(worldPosition);
    // 12: 기하 노멀 (노말맵 전). G-buffer 의 두 번째 옥타헤드럴 노멀이 맞게 디코딩되는지 봄.
    else if (debugView == 12) color = s.geometricNormal * 0.5f + 0.5f;
    else handled = false;
    return handled;
}

// 클러스터드 경로 (3단계): 방향광 directionalCount 개는 전부, 지역광은 이 픽셀 클러스터의 목록만.
// pixel = SV_Position.xy, viewZ = 뷰 공간 z. 목록의 번호가 오름차순이고 방향광이 t15 앞쪽에 모여 있으므로, 목록에서 빠진
// 조명(감쇠가 정확히 0)을 빼면 ShadeAllLights 와 같은 순서로 더함 → 두 경로의 화면이 같아야 함 (컬링이 보수적일 때).
float3 ShadeClusteredLights(SurfaceData s, float3 worldPosition, float3 viewDirection, float2 pixel, float viewZ, float3 color)
{
    const float NdotV = max(dot(s.normal, viewDirection), EPSILON);
    const uint dirCount = min(directionalCount, lightCount);
    [loop]
    for (uint i = 0; i < dirCount; ++i)
    {
        color += ShadeLight(s, lights[i], worldPosition, viewDirection, NdotV);
    }
    const uint2 cell = clusterGrid[ClusterIndex(pixel, viewZ)];
    [loop]
    for (uint j = 0; j < cell.y; ++j)
    {
        color += ShadeLight(s, lights[clusterLightIndices[cell.x + j]], worldPosition, viewDirection, NdotV);
    }
    return color;
}

// 클러스터드가 켜져 있으면(b0 clusterParams.x) 클러스터 목록, 아니면 전체 순회. 포워드·디퍼드가 이것을 부름.
float3 ShadeLights(SurfaceData s, float3 worldPosition, float3 viewDirection, float2 pixel, float viewZ, float3 color)
{
    return clusterParams.x > 0.5f
        ? ShadeClusteredLights(s, worldPosition, viewDirection, pixel, viewZ, color)
        : ShadeAllLights(s, worldPosition, viewDirection, color);
}

// 디버그 뷰 14 (클러스터 조명 수 히트맵: 0 검정 → 파랑 → 초록 → 빨강, 32 개 이상 흰색) · 15 (z 슬라이스 색띠).
// 클러스터드가 꺼져 있어도 슬라이스는 b0 값으로 계산되므로 보임 (14 는 목록이 비어 검정).
bool ClusterDebugColor(int debugView, float2 pixel, float viewZ, out float3 color)
{
    color = float3(0.0f, 0.0f, 0.0f);
    bool handled = false;
    if (debugView == 14)
    {
        const uint count = clusterParams.x > 0.5f ? clusterGrid[ClusterIndex(pixel, viewZ)].y : 0;
        const float t = saturate(count / 32.0f);
        color = count == 0 ? float3(0.0f, 0.0f, 0.0f)
            : (t < 0.5f ? lerp(float3(0.0f, 0.0f, 1.0f), float3(0.0f, 1.0f, 0.0f), t * 2.0f)
                        : lerp(float3(0.0f, 1.0f, 0.0f), float3(1.0f, 0.0f, 0.0f), t * 2.0f - 1.0f));
        if (count >= 32) color = float3(1.0f, 1.0f, 1.0f);
        handled = true;
    }
    else if (debugView == 15)
    {
        const uint slice = ClusterSlice(viewZ);
        // 슬라이스마다 색이 번갈아 바뀜 (빨강·초록·파랑 + 밝기 3 단계)
        const float3 hues[3] = { float3(1.0f, 0.25f, 0.25f), float3(0.25f, 1.0f, 0.25f), float3(0.25f, 0.25f, 1.0f) };
        color = hues[slice % 3] * (0.4f + 0.3f * ((slice / 3) % 3));
        handled = true;
    }
    return handled;
}

// 뷰 공간 z (월드 위치 기준). 포워드 경로가 클러스터 슬라이스를 고를 때 씀 (디퍼드는 깊이 버퍼에서 바로 구함).
float ViewDepthFromWorld(float3 worldPosition)
{
    return mul(float4(worldPosition, 1.0f), view).z;
}

#endif // SHERLOCK_SHADING_HLSLI

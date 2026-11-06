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

float3 FresnelSchlick(float3 F0, float cosTheta)
{
    return F0 + (1.0f - F0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

// 환경광용. 거칠수록 스치는 각도의 반사가 덜 세지도록 최대값을 1 − roughness 로 누름 (Lagarde/Sébastien).
float3 FresnelSchlickRoughness(float3 F0, float cosTheta, float roughness)
{
    return F0 + (max((1.0f - roughness).xxx, F0) - F0) * pow(1.0f - saturate(cosTheta), 5.0f);
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
// 반사 = 프리필터(R, 거칠기 밉) · (F0 · A + B), (A, B) = BRDF LUT(N·V, 1 − roughness).
// 환경 큐브가 없으면 두 조도를 ambientColor 하나로 대신함 — PBR 이전의 albedo · ambientColor 와 거의 같은 밝기.
float3 EnvironmentLighting(float3 albedo, float3 F0, float metallic, float roughness, float3 N, float3 V, float ao)
{
    const float NdotV = max(dot(N, V), 0.0f);
    const float3 F = FresnelSchlickRoughness(F0, NdotV, roughness);
    const float3 kd = (1.0f - F) * (1.0f - metallic);

    float3 irradiance = ambientColor;
    float3 prefiltered = ambientColor;
    float intensity = 1.0f;
    if (environmentParams.w > 0.5f)
    {
        irradiance = irradianceMap.SampleLevel(environmentSampler, N, environmentParams.z).rgb;
        const float maxLod = max(environmentParams.y - 1.0f, 0.0f);
        prefiltered = prefilteredMap.SampleLevel(environmentSampler, reflect(-V, N), roughness * maxLod).rgb;
        intensity = environmentParams.x;
    }
    const float2 brdf = outputParams.z > 0.5f
        ? brdfLut.SampleLevel(environmentSampler, float2(NdotV, 1.0f - roughness), 0.0f).rg
        : EnvBRDFApprox(roughness, NdotV);

    return (kd * albedo * irradiance + prefiltered * (F0 * brdf.x + brdf.y)) * intensity * ao;
}

float4 PS_Main(VSOutput input) : SV_TARGET
{
    // 재질의 기본색 × 정점 색 × 텍스처. sRGB 텍스처는 샘플링할 때 하드웨어가 선형으로 변환해 주므로
    // 여기서부터 모든 색은 선형 공간임. 백버퍼에 쓸 때 sRGB 뷰가 다시 인코딩함.
    const float4 texel = albedoTexture.Sample(albedoSampler, input.uv);
    const float4 surface = input.col * baseColor * texel;
    const float3 albedo = surface.rgb;

    // 알파 컷아웃 (glTF MASK). 나뭇잎·격자처럼 알파로 구멍을 낸 재질에 씀.
    if (alphaCutoff > 0.0f)
    {
        clip(surface.a - alphaCutoff);
    }

    // 감마 검증용: 조명 없이 알베도를 그대로 출력함. 128 회색 텍스처가 화면에서 128로 읽혀야 함.
    // HDR: 톤매핑이 톤 패스로 옮겨 가서 이 픽셀도 노출·ACES 를 거침 — 감마 검증은 톤매핑을 끄고 노출 1 에서 할 것.
    if (unlit > 0.5f)
    {
        return float4(albedo, surface.a);
    }

    // PBR 입력. 텍스처가 없으면 흰색이라 계수가 그대로 쓰임. 거칠기 하한은 GGX 의 0 나눗셈과 점광원의 바늘
    // 같은 하이라이트를 막음.
    const float4 metalRough = metallicRoughnessTexture.Sample(albedoSampler, input.uv);
    const float metal = saturate(metallic * metalRough.b);
    const float rough = clamp(roughness * metalRough.g, 0.03f, 1.0f);
    const float ao = lerp(1.0f, occlusionTexture.Sample(albedoSampler, input.uv).r, occlusionStrength);
    const float3 emission = emissive * emissiveTexture.Sample(albedoSampler, input.uv).rgb;
    // F0: 수직으로 볼 때의 반사율. 유전체는 4%, 금속은 자기 색. 금속은 확산이 없음(kd 의 1 − metallic).
    const float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metal);

    // 노멀 매핑. 탄젠트 공간(T, B, N) 의 노멀 맵 값을 월드 공간으로 변환함.
    // 노멀 맵은 선형 텍스처이고 (0.5, 0.5, 1) 이 "평평"을 뜻함. B 는 정점 탄젠트의 손잡이(w)로 복원함.
    const float3 geometricNormal = SafeNormalize(input.normal, float3(0.0f, 1.0f, 0.0f));
    const float3 T = SafeNormalize(input.tangent.xyz - geometricNormal * dot(geometricNormal, input.tangent.xyz), float3(1.0f, 0.0f, 0.0f));
    const float3 B = cross(geometricNormal, T) * input.tangent.w;
    float3 tangentNormal = normalTexture.Sample(albedoSampler, input.uv).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= normalStrength;
    const float3 normal = SafeNormalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * geometricNormal, geometricNormal);

    const float3 viewDirection = SafeNormalize(
        cameraPosition - input.worldPosition, float3(0.0f, 0.0f, -1.0f));

    // 디버그 뷰. 에디터의 Render Settings 에서 고름.
    const int debugView = (int)debugParams.x;
    if (debugView == 1) return float4(albedo, 1.0f);
    if (debugView == 2) return float4(normal * 0.5f + 0.5f, 1.0f);
    if (debugView == 3) { const float d = saturate(length(cameraPosition - input.worldPosition) / max(debugParams.y, 0.001f)); return float4(d, d, d, 1.0f); }
    if (debugView == 4)
    {
        // 그림자가 있는 모든 조명의 계수를 곱해 보여 줌
        float s = 1.0f;
        for (uint k = 0; k < min(lightCount, MAX_LIGHTS); ++k)
        {
            const LightData l = lights[k];
            const float d = length(l.position - input.worldPosition);
            s *= ShadowForLight(l, input.worldPosition, geometricNormal, normal - geometricNormal, d);
        }
        return float4(s, s, s, 1.0f);
    }
    if (debugView == 5) return float4(frac(input.uv), 0.0f, 1.0f);
    if (debugView == 6) return float4(0.0f, rough, metal, 1.0f);   // glTF 의 metallicRoughness 텍스처와 같은 채널 배치
    if (debugView == 7) return float4(ao, ao, ao, 1.0f);
    if (debugView == 8)
    {
        // 캐스케이드: 0 빨강, 1 초록, 2 파랑, 3 노랑, 마지막 너머는 어두운 회색, CSM 꺼짐은 흰색. 장면을
        // 알아보도록 알베도를 조금 섞음.
        float3 tint = float3(1.0f, 1.0f, 1.0f);
        if (cascadeOrigin.w > 0.5f)
        {
            const int c = SelectCascade(input.worldPosition);
            tint = float3(0.15f, 0.15f, 0.15f);
            if (c == 0) tint = float3(1.0f, 0.1f, 0.1f);
            else if (c == 1) tint = float3(0.1f, 1.0f, 0.1f);
            else if (c == 2) tint = float3(0.1f, 0.1f, 1.0f);
            else if (c == 3) tint = float3(1.0f, 1.0f, 0.1f);
        }
        return float4(lerp(tint, albedo, 0.3f), 1.0f);
    }

    const float NdotV = max(dot(normal, viewDirection), EPSILON);
    float3 finalColor = EnvironmentLighting(albedo, F0, metal, rough, normal, viewDirection, ao) + emission;
    const uint activeLightCount = min(lightCount, MAX_LIGHTS);

    [loop]
    for (uint i = 0; i < activeLightCount; ++i)
    {
        const LightData light = lights[i];
        float3 surfaceToLight = float3(0.0f, 1.0f, 0.0f);
        float attenuation = 1.0f;
        float distanceToLight = 0.0f;   // 점·스팟만 채움. 그림자의 원근 뷰가 텍셀 크기를 구할 때 씀

        if (light.type == LIGHT_DIRECTIONAL)
        {
            surfaceToLight = SafeNormalize(-light.direction, float3(0.0f, 1.0f, 0.0f));
        }
        else
        {
            const float3 toLight = light.position - input.worldPosition;
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

        const float NdotL = saturate(dot(normal, surfaceToLight));
        if (NdotL <= 0.0f)
        {
            continue;
        }
        // 빛이 닿지 않는 픽셀(원뿔 밖·범위 밖)은 PCF 를 건너뜀
        if (attenuation > 0.0f)
        {
            attenuation *= ShadowForLight(light, input.worldPosition, geometricNormal, normal - geometricNormal, distanceToLight);
        }
        const float3 halfVector = SafeNormalize(surfaceToLight + viewDirection, normal);
        const float NdotH = saturate(dot(normal, halfVector));
        const float VdotH = saturate(dot(viewDirection, halfVector));

        const float D = DistributionGGX(NdotH, rough * rough);
        const float G = GeometrySmith(NdotL, NdotV, rough);
        const float3 F = FresnelSchlick(F0, VdotH);
        const float3 specular = D * G * F / max(4.0f * NdotL * NdotV, EPSILON);
        const float3 kd = (1.0f - F) * (1.0f - metal);   // 반사된 몫과 금속을 뺀 나머지만 확산함 (에너지 보존)

        // 조도 관례(위 주석): BRDF 전체에 π 를 곱하므로 확산항은 kd · albedo, 반사항은 specular · π 가 됨.
        const float3 irradiance = light.color * max(light.intensity, 0.0f) * attenuation * NdotL;
        finalColor += (kd * albedo + specular * PI) * irradiance;
    }

    // HDR: 선형 HDR 값을 그대로 FP16 타깃에 씀 (1 을 넘어도 잘리지 않음). 노출·ACES 는 TonemapPixelShader 가 함.
    return float4(finalColor, surface.a);
}

// 재질 셋(Material.hlsli)과 메시 보간값(Mesh.hlsli 의 VSOutput)에서 SurfaceData 를 만듦.
// 포워드 BasicPS 와 G-buffer PS 가 같이 씀 — 두 경로가 재질을 똑같이 읽어야 디퍼드 화면이 포워드와 같아짐.
//
// 두 단계로 나뉨:
//   MaterialBaseColor        기본색·알파 (텍스처 × 정점 색 × baseColor). 알파 컷아웃과 unlit 판정이 이 값만 씀
//   EvaluateMaterialSurface  나머지 PBR 입력과 노멀 (노말맵·뒷면 뒤집기·클리어코트 노멀), 오브젝트별 강조·디졸브 경계 빛 (b1)
// 디졸브의 clip 은 PS 가 컷아웃 clip 바로 뒤에서 직접 부름 (DissolveClip) — 이 파일은 경계 빛만 발광에 더함.
#ifndef SHERLOCK_MATERIAL_SURFACE_HLSLI
#define SHERLOCK_MATERIAL_SURFACE_HLSLI

#include "Mesh.hlsli"
#include "Material.hlsli"
#include "Shading.hlsli"
#include "Dissolve.hlsli"

// 재질의 기본색 × 정점 색 × 텍스처. sRGB 텍스처는 샘플링할 때 하드웨어가 선형으로 변환해 주므로
// 여기서부터 모든 색은 선형 공간임.
float4 MaterialBaseColor(VSOutput input)
{
    const float4 texel = albedoTexture.Sample(albedoSampler, input.uv);
    return input.col * baseColor * texel;
}

// 오브젝트별 강조 (b1 PerObject 의 highlight*, C++ Scene/Highlight.h). 강조가 없는 오브젝트는 값이 모두 0 이라 lerp(…, 0) 과
// + 0 이 되어 결과가 강조 이전과 같음. 포워드의 unlit 경로(BasicPixelShader.hlsl)도 같은 두 함수를 씀.
// 기본색 섞기: F0 를 만들기 전에 섞어야 금속도 색이 바뀜 (금속은 확산이 없고 F0 = 기본색).
float3 HighlightAlbedo(float3 albedo)
{
    return lerp(albedo, highlightTint.rgb, saturate(highlightTint.a));
}

// 발광에 더할 값: 번쩍임 + 가장자리 빛. 가장자리 빛은 기하 노멀(노말맵 전)로 계산함 — 실루엣을 따라야 하고, 노말맵의 잔무늬가
// 반짝이지 않게. 시선 방향은 포워드 조명과 같은 식 (카메라 위치 − 픽셀 위치). 지수는 C++ 가 0.1 이상으로 넘기지만 여기서도 자름
// (0 지수의 pow(0, 0) 이 NaN 이 되지 않게).
float3 HighlightEmission(float3 geometricNormal, float3 worldPosition)
{
    const float3 viewDirection = SafeNormalize(cameraPosition - worldPosition, float3(0.0f, 0.0f, -1.0f));
    const float rim = pow(saturate(1.0f - saturate(dot(geometricNormal, viewDirection))), max(highlightRim.a, 0.1f));
    return highlightFlash.rgb + highlightRim.rgb * rim;
}

#ifndef RAIN_NO_WETNESS
// 젖음 (Lagarde, "Water drop 2a — Dynamic rain and its effects"). 다공성 면은 물이 스며 확산이 어두워지고, 표면에 얇은 물막이 생겨
// 거칠기가 내려감. 금속은 스며들지 않으므로 어두워지지 않음. 웅덩이는 물이 표면을 덮은 것 — 아주 매끈한 유전체 (물 F0 ≈ 0.02).
// SurfaceData 가 다 채워지기 전에 부르므로 (F0 전) 칸을 따로 받음. wet = puddle = 0 이면 그대로.
void ApplyWetness(inout float3 albedo, inout float metallicValue, inout float roughnessValue, float wet, float puddle)
{
    const float porous = 1.0f - metallicValue;
    albedo *= lerp(1.0f, 0.45f, wet * porous);
    roughnessValue = lerp(roughnessValue, min(roughnessValue, 0.12f), wet * 0.85f);
    // 웅덩이: 물이 바닥색을 더 어둡게 덮고 (반사가 대부분), 금속성도 덮음
    albedo *= lerp(1.0f, 0.6f, puddle);
    metallicValue = lerp(metallicValue, 0.0f, puddle);
    roughnessValue = lerp(roughnessValue, 0.03f, puddle);
}

float3 ApplyWetNormal(float3 normal, float3 geometricNormal, float3 worldPosition, float wet, float puddle)
{
    float3 n = normal;
    if (wet > 0.0f)
    {
        n = SafeNormalize(lerp(normal, geometricNormal, puddle), geometricNormal);
        // 물결: 위를 보는 젖은 면 (웅덩이에서 가장 셈). 기울기를 월드 xz 에 더함
        const float up = saturate((geometricNormal.y - 0.7f) / 0.3f);
        const float weight = up * saturate(wet * 2.0f - 1.0f) * lerp(0.4f, 1.0f, puddle);
        if (weight > 0.0f)
        {
            const float2 slope = RainRipple(worldPosition) * weight;
            n = SafeNormalize(n + float3(slope.x, 0.0f, slope.y), geometricNormal);
        }
    }
    return n;
}
#endif

// baseColorAlpha = MaterialBaseColor 의 결과. isFrontFace = SV_IsFrontFace (양면 재질의 뒷면 노멀을 뒤집음).
SurfaceData EvaluateMaterialSurface(VSOutput input, bool isFrontFace, float4 baseColorAlpha)
{
    SurfaceData s;
    s.albedo = HighlightAlbedo(baseColorAlpha.rgb);
    s.alpha = baseColorAlpha.a;

    // PBR 입력. 텍스처가 없으면 흰색이라 계수가 그대로 쓰임. 거칠기 하한은 GGX 의 0 나눗셈과 점광원의 바늘
    // 같은 하이라이트를 막음.
    const float4 metalRough = metallicRoughnessTexture.Sample(albedoSampler, input.uv);
    s.metallic = saturate(metallic * metalRough.b);
    s.roughness = clamp(roughness * metalRough.g, 0.03f, 1.0f);
    s.ao = lerp(1.0f, occlusionTexture.Sample(albedoSampler, input.uv).r, occlusionStrength);
#ifndef RAIN_NO_WETNESS
    // 비 (Rain.hlsli): 비를 맞는 면은 어두워지고 매끈해짐, 위를 보는 면에는 웅덩이. F0 를 만들기 전에 바꿔야 금속의 F0 도 따라감.
    // 비가 없으면 (rainParams.y = 0) wet = puddle = 0 이라 아무것도 바뀌지 않음.
    const float3 rainNormal = (isFrontFace ? 1.0f : -1.0f) * SafeNormalize(input.normal, float3(0.0f, 1.0f, 0.0f));
    const float rainWet = RainWetAmount(input.worldPosition, rainNormal);
    // 컷아웃 재질 (잎·철망) 에는 물이 고이지 않음. 디버그 뷰 18 의 B 는 재질을 모르므로 이런 면에도 웅덩이 자리를 보여 줌.
    const float rainPuddle = alphaCutoff > 0.0f ? 0.0f : RainPuddle(input.worldPosition, rainNormal.y, rainWet);
    ApplyWetness(s.albedo, s.metallic, s.roughness, rainWet, rainPuddle);
#endif
    s.emission = emissive * emissiveTexture.Sample(albedoSampler, input.uv).rgb;
    // F0: 수직으로 볼 때의 반사율. 유전체는 굴절률에서 나온 값(ior 1.5 → 4%)에 반사 색·세기를 곱한 것, 금속은
    // 자기 색. F90: 스치는 각도의 반사율 — 유전체는 specularFactor, 금속은 1. 금속은 확산이 없음(kd 의 1 − metallic).
    // KHR_materials_specular 기본값(factor 1, 흰색)이면 F0 = 0.04, F90 = 1 로 이전과 같음. three.js 와 같은 식.
    const float specularWeight = saturate(specularFactor * specularTexture.Sample(albedoSampler, input.uv).a);
    const float3 specularTint = specularColor * specularColorTexture.Sample(albedoSampler, input.uv).rgb;
    const float iorRatio = (max(ior, 1.0f) - 1.0f) / (max(ior, 1.0f) + 1.0f);
    const float3 dielectricF0 = min(iorRatio * iorRatio * specularTint, 1.0f) * specularWeight;
    s.F0 = lerp(dielectricF0, s.albedo, s.metallic);
    s.F90 = lerp(specularWeight, 1.0f, s.metallic);

    // 노멀 매핑. 탄젠트 공간(T, B, N) 의 노멀 맵 값을 월드 공간으로 변환함.
    // 노멀 맵은 선형 텍스처이고 (0.5, 0.5, 1) 이 "평평"을 뜻함. B 는 정점 탄젠트의 손잡이(w)로 복원함.
    // 양면 재질(doubleSided)의 뒷면: 정점 노멀은 앞면 쪽을 향하므로, 카메라 쪽을 보도록 뒤집음. 단면 재질은 뒷면이
    // 컬링되어 항상 앞면이라 아무 일도 없음. 탄젠트 B 는 아래에서 뒤집힌 노멀로 다시 만들어지므로 함께 따라 뒤집힘.
    const float faceSign = isFrontFace ? 1.0f : -1.0f;
    s.geometricNormal = faceSign * SafeNormalize(input.normal, float3(0.0f, 1.0f, 0.0f));
    const float3 T = SafeNormalize(input.tangent.xyz - s.geometricNormal * dot(s.geometricNormal, input.tangent.xyz), float3(1.0f, 0.0f, 0.0f));
    const float3 B = cross(s.geometricNormal, T) * input.tangent.w;
    float3 tangentNormal = normalTexture.Sample(albedoSampler, input.uv).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= normalStrength;
    s.normal = SafeNormalize(tangentNormal.x * T + tangentNormal.y * B + tangentNormal.z * s.geometricNormal, s.geometricNormal);
#ifndef RAIN_NO_WETNESS
    // 웅덩이는 물이 표면 요철을 덮어 평평 (기하 노멀 쪽으로), 젖은 위쪽 면에는 빗방울 물결.
    s.normal = ApplyWetNormal(s.normal, s.geometricNormal, input.worldPosition, rainWet, rainPuddle);
#endif

    // 클리어코트 (KHR_materials_clearcoat). 텍스처가 없으면 흰색이라 계수가 그대로 쓰임. 코팅 노멀 맵이 없으면 평평한
    // 노멀이 꽂혀 기하 노멀과 같아짐 — 코팅은 기본 층의 노멀 맵을 따르지 않는다는 glTF 규칙. 분기 안에서 샘플하지 않고
    // 항상 샘플함 (그래디언트 계산이 균일한 흐름에서만 안전하므로).
    s.clearcoat = saturate(clearcoatFactor * clearcoatTexture.Sample(albedoSampler, input.uv).r);
    s.clearcoatRoughness = clamp(clearcoatRoughness * clearcoatRoughnessTexture.Sample(albedoSampler, input.uv).g, 0.03f, 1.0f);
    float3 coatTangentNormal = clearcoatNormalTexture.Sample(albedoSampler, input.uv).xyz * 2.0f - 1.0f;
    coatTangentNormal.xy *= clearcoatNormalScale;
    s.clearcoatNormal = SafeNormalize(coatTangentNormal.x * T + coatTangentNormal.y * B + coatTangentNormal.z * s.geometricNormal, s.geometricNormal);

    // 투과 (KHR_materials_transmission): 비금속의 확산을 transmission 만큼 줄이고, 블렌딩 재질은 그만큼 뒤가 비치게
    // 알파를 낮춤 (BasicPS 끝). 반사(스페큘러)는 줄이지 않음. 굴절·틴트는 없음 — 뒤의 색이 그대로 비침.
    s.transmission = saturate(transmissionFactor * transmissionTexture.Sample(albedoSampler, input.uv).r) * (1.0f - s.metallic);

    // 오브젝트별 강조의 발광 몫과 디졸브 경계 빛. 포워드는 조명 결과에, 디퍼드는 G-buffer 의 발광 칸(view.hdr)에 그대로 실림.
    s.emission += HighlightEmission(s.geometricNormal, input.worldPosition) + DissolveEdgeEmission(DissolveNoise(input.uv));
    return s;
}

#endif // SHERLOCK_MATERIAL_SURFACE_HLSLI

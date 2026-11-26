// 디졸브 (docs/plan/object-highlight-record.html 7장). 오브젝트별 진행도·경계는 b1 PerObject 의 dissolveEdge(rgb 경계 빛, a 진행도)와 highlightFlash.a(경계 폭)
// (C++ Scene/Dissolve.h), 노이즈는 재질 셋의 t20 (Material::dissolveNoiseTexture, 비면 builtin:noise).
// 포워드 BasicPS · G-buffer PS · 그림자 알파 PS (ShadowAlphaPixelShader.hlsl) 가 같은 함수를 써서 몸과 그림자가 같이 사라짐.
//
// 진행도 a, 경계 폭 w 에서 문턱 t = a · (1 + w) − w. 노이즈 < t 인 픽셀은 버리고, t ≤ 노이즈 < t + w 인 띠에 경계 빛을 더함.
//   a = 0 → t = −w: 버리는 픽셀도 띠도 없음 (아래 함수는 a = 0 이면 아예 아무것도 하지 않음 — 디졸브 없는 화면이 예전과 같음)
//   a = 1 → t = 1: 노이즈 최댓값 1 까지 모두 버림 (clip 의 문턱에 작은 여유를 더해 노이즈 = 1 인 픽셀도 사라지게 함)
#ifndef SHERLOCK_DISSOLVE_HLSLI
#define SHERLOCK_DISSOLVE_HLSLI

#include "Mesh.hlsli"
#include "Material.hlsli"

// 노이즈 값. 분기 밖에서 항상 샘플함 (그래디언트 계산이 균일한 흐름에서만 안전하므로 — MaterialSurface.hlsli 의 클리어코트와 같은 이유).
// uv 는 VS 가 uvScale 을 곱해 넘긴 값. 샘플러는 재질의 것 (Clamp 프리셋이면 노이즈도 타일링되지 않음).
float DissolveNoise(float2 uv)
{
    return dissolveNoiseTexture.Sample(albedoSampler, uv * dissolveNoiseScale).r;
}

float DissolveAmount()
{
    return dissolveEdge.a;
}

float DissolveWidth()
{
    return max(highlightFlash.a, 0.0f);
}

float DissolveThreshold()
{
    const float width = DissolveWidth();
    return DissolveAmount() * (1.0f + width) - width;
}

// 노이즈가 문턱보다 작으면 픽셀을 버림. 진행도 0 이면 아무것도 하지 않음.
void DissolveClip(float noise)
{
    if (DissolveAmount() > 0.0f)
    {
        clip(noise - DissolveThreshold() - 0.0001f);
    }
}

// 경계 띠의 발광 (문턱에서 1, 문턱 + w 에서 0). 진행도나 폭이 0 이면 0.
float3 DissolveEdgeEmission(float noise)
{
    float3 result = float3(0.0f, 0.0f, 0.0f);
    if (DissolveAmount() > 0.0f && DissolveWidth() > 0.0f)
    {
        result = dissolveEdge.rgb * (1.0f - saturate((noise - DissolveThreshold()) / DissolveWidth()));
    }
    return result;
}

#endif // SHERLOCK_DISSOLVE_HLSLI

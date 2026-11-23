// Gerstner 바다 (docs/plan/ocean-gerstner-plan.html). b9 Ocean 과 파도 합 함수.
// 상수버퍼 배치는 Graphics/ShaderConstants.h 의 OceanConstants 와 바이트 단위로 같아야 함.
//
// 평면 위 원래 위치 x₀ = (x, z) 에서 파도 i (방향 Dᵢ, 파수 kᵢ, 진폭 Aᵢ, 경사 sᵢ, 위상 φᵢ) 를 더함 (Y 위):
//   θᵢ   = kᵢ (Dᵢ · x₀) − φᵢ
//   P.xz = x₀ + Σ (sᵢ / kᵢ) Dᵢ cos θᵢ     수평 변위: 마루 쪽으로 점이 모여 마루가 뾰족해짐
//   P.y  =      Σ  Aᵢ          sin θᵢ     높이
//   N    = normalize(cross(∂P/∂z, ∂P/∂x)) 평평할 때 (0, 1, 0)
// Σ sᵢ ≤ 1 이면 마루에서 표면이 겹쳐 뒤집히지 않음 (C++ 가 자름). 수식은 Projects/SherlockTestScenes/Scripts/Tests/
// check_gerstner_normals.py 가 유한 차분과 대조함.
//
// 계산은 월드 xz 에서 함 (정점을 먼저 오브젝트 world 로 옮긴 뒤 더함). 바다 오브젝트를 옮기거나 요로 돌려도 파도 방향은
// 월드 기준. 피치·롤은 지원하지 않음 (파도는 항상 월드 Y 로 솟음).
//
// 3단계 (픽셀 단위 노멀): 바다 PS (OceanPixelShader·OceanGBufferPixelShader) 가 변위 전 xz 로 GerstnerSampleFiltered 를 다시
// 불러 노멀을 구함 — 격자 간격과 무관하게 매끈함. 화면 픽셀 하나가 덮는 xz 범위(ddx·ddy)가 파장에 비해 크면 그 파도의 기울기를
// 빼서(대역 제한) 먼 바다에서 픽셀보다 짧은 파도가 점 표본으로 반짝이는 것을 막음.
#ifndef SHERLOCK_OCEAN_HLSLI
#define SHERLOCK_OCEAN_HLSLI

#include "Mesh.hlsli"   // VSOutput

static const uint MAX_OCEAN_WAVES = 8;   // ShaderConstants.h 의 kMaxOceanWaves 와 같아야 함

// b9: 프레임에 한 번. 바다 VS·PS. (288바이트)
cbuffer Ocean : register(b9)
{
    float4 oceanWaveA[MAX_OCEAN_WAVES];   // x = Dx, y = Dz, z = k, w = A
    float4 oceanWaveB[MAX_OCEAN_WAVES];   // x = s, y = 이번 위상, z = 지난 위상, w = 0
    float4 oceanParams;                   // x = 파도 수, y = fadeStart, z = fadeEnd (≤ fadeStart 면 감쇠 없음)
    float4 oceanOrigin;                   // xz = 감쇠 기준점 (바다 오브젝트 월드 원점)
}

static const uint OCEAN_PHASE_NOW = 0;    // 이번 프레임 (그리는 위치)
static const uint OCEAN_PHASE_PREV = 1;   // 지난 프레임 (속도 버퍼의 이전 위치)

// 바다 원점에서의 거리로 진폭·경사를 1 → 0 으로 줄이는 배율. 카메라가 아니라 바다 원점 기준이라 이번·지난 위치에 같은 값이
// 곱해져 속도가 틀어지지 않음. 노멀은 이 배율의 기울기를 무시함 (감쇠 구간이 파장보다 훨씬 넓어 차이가 작음).
float OceanFade(float2 xz)
{
    const float fadeStart = oceanParams.y;
    const float fadeEnd = oceanParams.z;
    float fade = 1.0f;
    if (fadeEnd > fadeStart)
    {
        fade = 1.0f - smoothstep(fadeStart, fadeEnd, distance(xz, oceanOrigin.xz));
    }
    return fade;
}

uint OceanWaveCount()
{
    return min((uint)oceanParams.x, MAX_OCEAN_WAVES);
}

float OceanPhase(uint i, uint phaseSlot)
{
    return phaseSlot == OCEAN_PHASE_PREV ? oceanWaveB[i].z : oceanWaveB[i].y;
}

// 변위된 위치만 (속도 버퍼의 지난 위치용).
float3 GerstnerPosition(float3 basePosition, uint phaseSlot)
{
    const float2 x0 = basePosition.xz;
    const float fade = OceanFade(x0);
    float3 p = basePosition;
    const uint count = OceanWaveCount();
    [loop]
    for (uint i = 0; i < count; ++i)
    {
        const float2 dir = oceanWaveA[i].xy;
        const float k = oceanWaveA[i].z;
        const float amplitude = oceanWaveA[i].w * fade;
        const float steepness = oceanWaveB[i].x * fade;
        float s, c;
        sincos(k * dot(dir, x0) - OceanPhase(i, phaseSlot), s, c);
        p.xz += (steepness / k) * dir * c;
        p.y += amplitude * s;
    }
    return p;
}

struct OceanSample
{
    float3 position;   // 변위된 월드 위치
    float3 normal;     // 월드 노멀 (단위)
    float3 dPdx;       // 원래 x 방향 편미분 (탄젠트를 만들 때 씀)
    float3 dPdz;       // 원래 z 방향 편미분
};

// 대역 제한 배율: 파도 방향으로 픽셀 하나가 덮는 주기 수가 0.1 (파장당 10 픽셀) → 0.25 (4 픽셀) 사이에서 1 → 0.
// dx0·dy0 = 화면 x·y 로 한 픽셀 옮길 때 변위 전 xz 의 변화 (ddx·ddy). 0 이면 1 (정점 셰이더).
// 구간은 OceanHorizon.json (TAA 끔) 에서 정함 — 끔 / 0.25~0.5 / 0.1~0.25 에서 먼 바다 외딴 점 1251 / 906 / 412,
// 연속 두 프레임 사이 깜빡임 149 / 31 / 25 픽셀. 가까운 바다는 세 경우가 같아 보이고 먼 해 반사 줄만 조금 약해짐.
float OceanBandLimit(float2 dir, float k, float2 dx0, float2 dy0)
{
    const float cyclesPerPixel = k * length(float2(dot(dx0, dir), dot(dy0, dir))) * (1.0f / 6.28318531f);
    return 1.0f - smoothstep(0.1f, 0.25f, cyclesPerPixel);
}

// 위치·노멀·편미분. dx0·dy0 가 0 이 아니면 노멀·편미분만 대역 제한 (위치는 그대로 — PS 는 위치를 쓰지 않음).
OceanSample GerstnerSampleFiltered(float3 basePosition, uint phaseSlot, float2 dx0, float2 dy0)
{
    const float2 x0 = basePosition.xz;
    const float fade = OceanFade(x0);
    OceanSample result;
    result.position = basePosition;
    result.dPdx = float3(1.0f, 0.0f, 0.0f);
    result.dPdz = float3(0.0f, 0.0f, 1.0f);
    const uint count = OceanWaveCount();
    [loop]
    for (uint i = 0; i < count; ++i)
    {
        const float2 dir = oceanWaveA[i].xy;
        const float k = oceanWaveA[i].z;
        const float amplitude = oceanWaveA[i].w * fade;
        const float steepness = oceanWaveB[i].x * fade;
        float s, c;
        sincos(k * dot(dir, x0) - OceanPhase(i, phaseSlot), s, c);
        result.position.xz += (steepness / k) * dir * c;
        result.position.y += amplitude * s;
        const float band = OceanBandLimit(dir, k, dx0, dy0);
        const float ka = k * amplitude * band;
        const float sb = steepness * band;
        // ∂P/∂x = (1 − Σ s Dx² sin, Σ kA Dx cos, − Σ s Dx Dz sin), ∂P/∂z = (− Σ s Dx Dz sin, Σ kA Dz cos, 1 − Σ s Dz² sin)
        result.dPdx += float3(-sb * dir.x * dir.x * s, ka * dir.x * c, -sb * dir.x * dir.y * s);
        result.dPdz += float3(-sb * dir.x * dir.y * s, ka * dir.y * c, -sb * dir.y * dir.y * s);
    }
    result.normal = normalize(cross(result.dPdz, result.dPdx));
    return result;
}

OceanSample GerstnerSample(float3 basePosition, uint phaseSlot)
{
    return GerstnerSampleFiltered(basePosition, phaseSlot, float2(0.0f, 0.0f), float2(0.0f, 0.0f));
}

// 바다 VS 출력: 공용 VSOutput 뒤에 변위 전 정보를 덧붙임 (다른 PS 의 입력 시그니처는 그대로).
struct OceanVSOutput
{
    VSOutput base;
    float4 oceanBase : TEXCOORD4;   // xy = 변위 전 월드 xz, zw = 월드로 옮긴 메시 탄젠트의 xz (탄젠트 재구성용)
};

// 메시 탄젠트(UV u 방향)의 xz 를 변위된 표면 위 방향으로. 평평할 때 = 메시 탄젠트. 길이가 0 이면 ∂P/∂x.
float3 OceanSurfaceTangent(OceanSample o, float2 meshTangentXZ)
{
    const float3 tangent = o.dPdx * meshTangentXZ.x + o.dPdz * meshTangentXZ.y;
    return normalize(dot(tangent, tangent) > 1e-12f ? tangent : o.dPdx);
}

// 픽셀 셰이더: 노멀·탄젠트를 픽셀에서 다시 계산한 VSOutput (나머지 값은 VS 그대로). ddx·ddy 를 쓰므로 분기 밖에서 부를 것.
VSOutput OceanPixelInput(OceanVSOutput input)
{
    VSOutput v = input.base;
    const float2 x0 = input.oceanBase.xy;
    const OceanSample o = GerstnerSampleFiltered(float3(x0.x, 0.0f, x0.y), OCEAN_PHASE_NOW, ddx(x0), ddy(x0));
    v.normal = o.normal;
    v.tangent.xyz = OceanSurfaceTangent(o, input.oceanBase.zw);
    return v;
}

#endif // SHERLOCK_OCEAN_HLSLI

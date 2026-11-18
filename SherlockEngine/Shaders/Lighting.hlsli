// 프레임 셋 (Layout_Frame) 중 조명·그림자·환경 — b0 PerFrame 은 Common.hlsli.
// 상수버퍼와 구조체 배치는 Graphics/ShaderConstants.h 와 바이트 단위로 같아야 함.
//
//   b2 Lights, t15 조명 배열, b5 ShadowPass (그림자 패스 VS), b6 Shadows,
//   t8 그림자 맵, s4 비교 샘플러, t14 CSM 아틀라스, t9 스카이박스, s5 환경 샘플러,
//   t10 IBL 확산 조도, t11 IBL 반사 프리필터, t12 BRDF LUT
#ifndef SHERLOCK_LIGHTING_HLSLI
#define SHERLOCK_LIGHTING_HLSLI

#include "Common.hlsli"

static const uint MAX_SHADOW_VIEWS = 20;   // Scene/Light.h 의 MAX_SHADOW_VIEWS 와 같아야 함 (b6 크기 2000B 가 이 값에 달림)
static const uint LIGHT_DIRECTIONAL = 0;
static const uint LIGHT_POINT = 1;
static const uint LIGHT_SPOT = 2;

// 80바이트 = 16바이트 레지스터 5개. float3 뒤에 스칼라를 두어 경계를 맞춤.
struct LightData
{
    float3 position;
    float intensity;

    float3 direction;
    float range;

    float3 color;
    uint type;

    float3 attenuation;
    float innerConeCos;

    float outerConeCos;

    // shadow옵션
    uint castShadows;
    int shadowIndex;
    float padding;
};

// b2: 조명 공통 값. PS만. (32바이트)
cbuffer Lights : register(b2)
{
    float3 ambientColor;
    uint lightCount;          // 아래 lights 의 유효 원소 수
    uint directionalCount;    // lights 의 앞쪽 이만큼이 방향광 (C++ BuildGpuLights 가 앞으로 모음). 클러스터드 경로가 씀
    uint3 lightsPadding;
}

// t15: 조명 배열 (프레임 셋). cbuffer 의 고정 크기 배열이 아니라 구조체 버퍼라 조명 수에 상한이 없음.
// 원소 배치는 Graphics/ShaderConstants.h 의 GpuLight 와 같아야 함 (80바이트).
StructuredBuffer<LightData> lights : register(t15);

struct ShadowViewData
{
    matrix viewProj;
    float4 atlasRect;
    float4 params;
};

// b5: 그림자 패스. VS 만. 뷰마다 다시 올림 (64바이트)
cbuffer ShadowPass : register(b5)
{
    matrix shadowViewProj;
}

// b6: 그림자 뷰 배열. PS 만 (2000바이트)
cbuffer Shadows : register(b6)
{
    ShadowViewData shadowViews[MAX_SHADOW_VIEWS];
    float4 cascadeSplits;
    float4 cascadeOrigin;
    float4 cascadeForward;
    float4 shadowAtlasParams;
    float4 csmAtlasParams;    // xy = 1/CSM 아틀라스 (가로, 세로)
}

// 그림자 맵은 R32_FLOAT 로 읽는 깊이 텍스처.
Texture2D shadowMap : register(t8);
SamplerComparisonState shadowSampler : register(s4);
// 방향광 CSM 전용 아틀라스 (캐스케이드마다 해상도가 다름). ShadowViewData.params.w == 1 인 뷰가 읽음. 같은
// 비교 샘플러(s4)를 씀.
Texture2D csmMap : register(t14);
// 씬의 스카이박스 큐브맵 (없으면 1×1 검은 큐브). 방향 벡터로 샘플함 — 길이는 상관없음.
// 프레임 셋에 둔 이유: 스카이박스뿐 아니라 이후 반사·환경광(IBL)도 모든 재질이 같은 큐브를 읽기 때문.
TextureCube environmentMap : register(t9);
SamplerState environmentSampler : register(s5);
// IBL. 셋 다 같은 선형 Clamp 샘플러(s5)로 읽음.
//   t10 확산 조도: 표면 노멀 방향의 코사인 가중 평균 복사휘도. albedo 를 곱하면 곧 확산 반사광 (π 는 이미 들어 있음)
//   t11 반사 프리필터: 반사 방향으로 읽음. 밉 0 = 거칠기 0(거울), 마지막 밉 = 거칠기 1
//   t12 BRDF LUT: u = N·V, v = 1 − roughness. RG = F0 에 곱할 값과 더할 값 (Karis 의 split-sum)
// 씬에 IBL 맵이 없고 스카이박스만 있으면 Renderer 가 스카이박스를 t10·t11 에 대신 꽂음(밉으로 근사).
TextureCube irradianceMap : register(t10);
TextureCube prefilteredMap : register(t11);
Texture2D brdfLut : register(t12);

#endif // SHERLOCK_LIGHTING_HLSLI

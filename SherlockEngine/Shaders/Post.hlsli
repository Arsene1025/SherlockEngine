// 후처리: 후처리 셋 (Layout_Post), 컴퓨트 블러 셋 (Layout_BlurCompute), 블러 가중치 셋 (Layout_BlurWeights) 이 같이 쓰는 선언.
// 상수버퍼 배치는 Graphics/ShaderConstants.h 와 바이트 단위로 같아야 함.
//
//   b4 PostParams, t13 입력, s6 선형 Clamp 샘플러 (후처리 셋만), t20 블러 가중치. 후처리 셋은 b0 PerFrame (Common.hlsli) 도 씀.
#ifndef SHERLOCK_POST_HLSLI
#define SHERLOCK_POST_HLSLI

#include "Common.hlsli"

// b4: 후처리 패스 파라미터. 후처리 셋(톤 패스·블러·블룸)만 바인딩함. (80바이트, ShaderConstants.h 의
// PostParamsConstants)
cbuffer PostParams : register(b4)
{
    float4 postPass;          // x = 모드 (1 블러, 2 블룸 밝기 추출+축소, 3 축소, 4 확대), y,z = 블러 방향 (텍셀 단위)
    float4 postBlur;          // 블러: x = 시그마(픽셀), y = 반경(픽셀). 모션 블러(xyz)·TAA(x)는 다른 뜻으로 씀 — 각 셰이더 머리말
    float4 postBloom;         // x = 임계값, y = knee, z = 확대 결과 배율
    float4 postView;          // xy = 입력 크기, zw = 출력 크기 (픽셀)
    float4 postGrade;         // x = GreyScale 양, y = 1 이면 색 효과를 건너뜀
}

// t13: 이 패스의 입력 (앞 단계의 결과). 후처리 PS (블러·블룸·톤매핑) 와 컴퓨트 블러가 같은 레지스터로 읽음 —
// 레지스터는 C++ 의 kSRVSlotSceneHdr. 재질 알베도 t0 과 겹치지 않게 t13 을 씀. 컴퓨트 블러는 SRV 로만 읽음 (cs_5_0 은 float4
// UAV 를 읽을 수 없음).
Texture2D postInput : register(t13);
// s6: 선형 Clamp (블룸 축소·확대). 후처리 셋에만 있음 — 다른 후처리 셰이더는 Load 를 씀.
SamplerState postSampler : register(s6);

// 블러 가우시안 가중치 w[i] = exp(-i^2 / 2σ^2), i = 0..반경 (반쪽만, 대칭). BlurWeightsComputeShader 가 블러마다 쓰고
// PS 블러(PostPixelShader 모드 1)와 CS 블러(BlurComputeShader)가 같은 버퍼를 읽음 — 두 경로가 가중치를 루프 안에서
// 따로 계산하던 때는 D3D11 에서 결과가 최하위 비트만큼 달랐음 (어느 연산 때문인지는 특정하지 못함). 후처리 셋·컴퓨트 블러 셋의 t20.
StructuredBuffer<float> blurWeights : register(t20);
static const int kMaxBlurRadius = 64;   // ShaderConstants.h 의 kMaxBlurRadius 와 같아야 함 (가중치 버퍼 = 65 개)

// 최종 출력. 노출을 곱하고, 켜져 있으면 ACES 근사(Narkowicz 2016)로 [0, ∞) → [0, 1] 로 누름.
// 꺼져 있으면 1 을 넘는 값은 출력 타깃에서 잘림. 결과는 선형 — sRGB RTV 가 인코딩함.
// HDR: 이 함수는 TonemapPixelShader 만 부름 (메인·스카이 PS 는 선형 HDR 값을 FP16 타깃에 그대로 씀).
// 그래서 "sRGB RTV" 는 톤 패스의 출력 뷰임.
float3 ApplyExposureToneMap(float3 color)
{
    color *= outputParams.x;
    if (outputParams.y > 0.5f)
    {
        color = saturate((color * (2.51f * color + 0.03f)) / (color * (2.43f * color + 0.59f) + 0.14f));
    }
    return color;
}

#endif // SHERLOCK_POST_HLSLI

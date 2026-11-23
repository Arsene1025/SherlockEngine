// 시간 셋 (Layout_Temporal): 속도 디버그·모션 블러·TAA 가 같이 쓰는 선언 (docs/plan/velocity-motionblur-taa-plan.html).
//   b0 PerFrame (Common.hlsli), b4 PostParams · t13 입력 · s6 선형 Clamp (Post.hlsli), t23 깊이, t7 속도, t24 TAA 히스토리.
// 셋은 뷰마다 (ViewTarget::temporalSet, TAA 는 history 핑퐁마다 한 벌씩).
#ifndef SHERLOCK_TEMPORAL_HLSLI
#define SHERLOCK_TEMPORAL_HLSLI

#include "Post.hlsli"
#include "Velocity.hlsli"

Texture2D<float> temporalDepth : register(t23);       // 씬 깊이 (DepthRead 상태)
Texture2D<float2> velocityTexture : register(t7);    // uv(이번) − uv(지난), UV 단위 (Velocity.hlsli)
Texture2D<float4> historyTexture : register(t24);     // TAA: 지난 프레임 결과 (다른 패스는 흰 텍스처)

// 화면 픽셀 → 이 픽셀의 속도 (픽셀 단위). 화면 밖은 가장자리 값.
float2 LoadVelocityPixels(int2 pixel)
{
    const int2 clamped = clamp(pixel, int2(0, 0), int2(screenParams.xy) - 1);
    return velocityTexture.Load(int3(clamped, 0)) * screenParams.xy;
}

float LoadDeviceDepth(int2 pixel)
{
    const int2 clamped = clamp(pixel, int2(0, 0), int2(screenParams.xy) - 1);
    return temporalDepth.Load(int3(clamped, 0));
}

// 장치 깊이(D24, 표준 Z: 가까움 0 → 멂 1) → 카메라 앞 거리. clusterParams: y = 원근(1)/직교(0), z = near, w = far.
//   원근: d = f/(f−n) · (1 − n/z) 를 z 로 풀면 z = n·f / (f − d·(f − n)).  직교: z = n + d·(f − n).
float LinearViewDepth(float deviceDepth)
{
    const float n = clusterParams.z;
    const float f = clusterParams.w;
    return clusterParams.y > 0.5f ? n * f / (f - deviceDepth * (f - n)) : n + deviceDepth * (f - n);
}

// Interleaved Gradient Noise (Jimenez 2014). 픽셀 좌표만 씀 — 프레임과 무관해서 스크린샷이 실행마다 같음.
float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

#endif // SHERLOCK_TEMPORAL_HLSLI

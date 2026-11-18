// SSAO (5단계, 화면 공간 앰비언트 오클루전) 의 SSAO 셋 (Layout_Ssao, 뷰마다 두 벌).
//
// 원리: 픽셀의 뷰 공간 위치 P 와 노멀 N 으로 반구(반경 radius)를 세우고, 그 안의 점 sampleCount 개를 화면에 투영해
// 깊이 버퍼와 비교함. 점보다 앞에(카메라 쪽에) 다른 표면이 있으면 그 방향은 가려진 것. 가려진 비율 → AO.
// 결과는 환경광(IBL)에만 곱함 — 직접광의 가림은 그림자가 맡음.
//
//   b12 SsaoParams  반경·바이어스·세기·거듭제곱·샘플 수 + 반구 커널 (C++ UploadSsaoConstants, 시드 고정)
//   t23 깊이 (DepthRead), t26 GB1 (옥타헤드럴 노멀), t29 입력 AO (블러 패스만 — SSAO 패스는 흰색)
//   b0 PerFrame (proj·view·화면 크기) 는 Common.hlsli
#ifndef SHERLOCK_SSAO_HLSLI
#define SHERLOCK_SSAO_HLSLI

#include "Common.hlsli"

static const uint MAX_SSAO_SAMPLES = 32;   // Graphics/ShaderConstants.h 의 kMaxSsaoSamples

// b12 (544 바이트). C++ SsaoConstants 와 같은 배치.
cbuffer SsaoParams : register(b12)
{
    float ssaoRadius;          // 월드(=뷰) 단위 반경
    float ssaoBias;            // 비교 여유 (뷰 z)
    float ssaoIntensity;
    float ssaoPower;
    uint ssaoSampleCount;
    uint3 ssaoPadding;
    float4 ssaoKernel[MAX_SSAO_SAMPLES];   // 탄젠트 공간 반구 안의 점 (z = 노멀 쪽, 길이 ≤ 1)
}

Texture2D<float> ssaoDepth : register(t23);
Texture2D<float4> ssaoNormals : register(t26);
Texture2D<float> ssaoInput : register(t29);

// 픽셀 → 깊이 버퍼 값. 화면 밖 좌표는 가장자리로 붙임.
float LoadSsaoDepth(int2 pixel)
{
    const int2 size = int2(screenParams.xy);
    return ssaoDepth.Load(int3(clamp(pixel, int2(0, 0), size - 1), 0));
}

#endif // SHERLOCK_SSAO_HLSLI

#include "Post.hlsli"

// 블러 가우시안 가중치를 구조체 버퍼에 씀 (버퍼 UAV 의 첫 소비자). 블러 패스마다 한 번, 블러 직전에 돌림.
// PS 블러와 CS 블러가 이 버퍼(t20)를 함께 읽어 같은 가중치를 씀 — 가중치를 각 블러 루프 안에서 계산하던 때는 D3D11 에서
// CS 와 PS 의 결과가 최하위 비트만큼 달랐음 (가중치 계산과 누적 중 어느 연산 탓인지는 특정하지 못함).
//
//   b4 : PostParams (postBlur.x = 시그마, postBlur.y = 반경)
//   u0 : 가중치 w[i] = exp(-i^2 / 2σ^2), i = 0..반경. 정규화는 읽는 쪽이 합으로 나눔
//
// 스레드 하나가 원소 하나. Dispatch 는 ceil((반경 + 1) / 64) 그룹이고, 반경을 넘는 스레드는 쓰지 않음.
RWStructuredBuffer<float> blurWeightsOut : register(u0);

[numthreads(64, 1, 1)]
void CS_BlurWeights(uint3 id : SV_DispatchThreadID)
{
    const int radius = min((int)postBlur.y, kMaxBlurRadius);
    const int i = (int)id.x;
    if (i > radius) return;

    const float sigma = max(postBlur.x, 0.01f);
    const float invTwoSigmaSq = 0.5f / (sigma * sigma);
    blurWeightsOut[i] = exp(-(float)(i * i) * invTwoSigmaSq);
}

// 알파 이미터의 뷰별 정렬 셋 (Layout_ParticleSort, Layout_ParticleSortStep — 바이토닉, CS_ParticleSortInit /
// CS_ParticleSortStep). 셋 0 = 이미터·뷰마다, 셋 1 = 단계마다.
#ifndef SHERLOCK_PARTICLE_SORT_HLSLI
#define SHERLOCK_PARTICLE_SORT_HLSLI

#include "Particle.hlsli"

//   t21 파티클, t22 이번 프레임 생존 목록, t24 개수 (kCounterSimCount = 이번 프레임 최종 생존 수)
//   u0 키 (뷰 공간 z), u1 파티클 번호. 길이 = capacity 를 2 의 거듭제곱으로 올린 것 (ParticleSortCount)
//   b10 이 뷰의 view 행렬, b11 이 단계의 (k, j)
StructuredBuffer<Particle> sortParticles : register(t21);
StructuredBuffer<uint> sortAliveList : register(t22);
StructuredBuffer<uint> sortCounters : register(t24);
RWStructuredBuffer<float> sortKeys : register(u0);
RWStructuredBuffer<uint> sortIndices : register(u1);
cbuffer ParticleSortView : register(b10)
{
    matrix sortView;
}
cbuffer ParticleSortStep : register(b11)
{
    uint sortK;            // 비교하는 바이토닉 열의 길이 (2, 4, … N)
    uint sortJ;            // 비교 거리 (k/2, … 1)
    uint2 sortStepPadding;
    float4 sortStepPadding2[15];   // 256 바이트 (D3D12 가 CB 를 256 으로 올림 — 초기 데이터 크기와 맞춤)
}
static const uint kSortCounterSimCount = 2;   // PARTICLE_SIM 의 kCounterSimCount 와 같음

// 정렬 길이: capacity 이상인 가장 작은 2 의 거듭제곱 (최소 64 = 그룹 하나). ShaderConstants.h 의 ParticleSortCount 와 같은 식.
uint ParticleSortCount(uint capacity)
{
    return capacity <= 64 ? 64 : (1u << (firstbithigh(capacity - 1) + 1));
}

#endif // SHERLOCK_PARTICLE_SORT_HLSLI

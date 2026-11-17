#define PARTICLE_SORT
#include "Common.hlsli"

// 바이토닉 정렬 한 단계 (VFX 3단계). CPU 가 k = 2, 4, … N 마다 j = k/2, … 1 순서로 디스패치함 (N = 2^m 이면
// m(m+1)/2 번). (k, j) 는 단계마다 미리 만들어 둔 상수 버퍼 (b11) — 디스패치마다 다른 값을 Dynamic 버퍼 하나에
// 다시 올릴 수 없음 (D3D12 의 프레임당 한 번 규칙).
//
// 스레드 i 가 짝 l = i ^ j 와 비교 (l > i 인 쪽만 일함). i & k == 0 인 열은 내림차순, 아니면 오름차순으로 맞추면
// 마지막 단계 (k = N) 뒤 전체가 내림차순 (먼 것부터).
[numthreads(64, 1, 1)]
void CS_ParticleSortStep(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleSortCount(particleCapacity)) return;
    const uint l = i ^ sortJ;
    if (l <= i) return;
    const float a = sortKeys[i];
    const float b = sortKeys[l];
    const bool descending = (i & sortK) == 0;
    if (descending ? (a < b) : (a > b))
    {
        sortKeys[i] = b;
        sortKeys[l] = a;
        const uint t = sortIndices[i];
        sortIndices[i] = sortIndices[l];
        sortIndices[l] = t;
    }
}

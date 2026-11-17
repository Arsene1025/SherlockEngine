#define PARTICLE_SORT
#include "Common.hlsli"

// 알파 파티클 정렬 준비 (VFX 3단계). 뷰마다, 이미터마다 한 번. 생존 목록의 앞 생존 수 칸을 (키 = 이 뷰의 뷰 공간 z,
// 값 = 파티클 번호) 로 옮기고, 나머지 칸 (2 의 거듭제곱까지) 은 가장 가까운 키로 채워 정렬 뒤 끝으로 가게 함.
// 정렬은 먼 것부터 (내림차순) — 알파 블렌딩은 뒤에서 앞으로 그려야 함.
//
// Dispatch = ParticleSortCount(capacity) / 64 그룹.
[numthreads(64, 1, 1)]
void CS_ParticleSortInit(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleSortCount(particleCapacity)) return;
    if (i < sortCounters[kSortCounterSimCount])
    {
        const uint index = sortAliveList[i];
        sortKeys[i] = mul(float4(sortParticles[index].position, 1.0f), sortView).z;
        sortIndices[i] = index;
    }
    else
    {
        sortKeys[i] = -3.0e38f;
        sortIndices[i] = 0;
    }
}

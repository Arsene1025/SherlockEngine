#include "ParticleSort.hlsli"

// 알파 파티클 정렬 준비 (VFX 3단계). 뷰마다, 이미터마다 한 번. 생존 목록의 앞 생존 수 칸을 (키 = 이 뷰의 뷰 공간 z,
// 값 = 파티클 번호) 로 옮기고, 나머지 칸 (2 의 거듭제곱까지) 은 가장 가까운 키로 채워 정렬 뒤 끝으로 가게 함.
// 정렬은 먼 것부터 (내림차순) — 알파 블렌딩은 뒤에서 앞으로 그려야 함.
//
// Dispatch = ParticleSortCount(capacity) / 64 그룹.
[numthreads(64, 1, 1)]
void CS_ParticleSortInit(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    //정렬 길이 밖 스레드는 일 없음 (길이가 64 의 배수라 실제로는 안 걸림)
    if (i >= ParticleSortCount(particleCapacity)) return;
    //앞 생존 수 칸: 실제 생존 입자
    if (i < sortCounters[kCounterSimCount])
    {
        const uint index = aliveList[i];
        //키 = 이 뷰 카메라 기준 뷰 공간 z (클수록 멂)
        sortKeys[i] = mul(float4(particles[index].position, 1.0f), sortView).z;
        sortIndices[i] = index;
    }
    else
    {
        //나머지 칸: 가장 작은 키 → 내림차순 정렬하면 맨 끝으로 감
        //그리기는 생존 수 칸까지만 읽어서 안 보임
        sortKeys[i] = -3.0e38f;
        sortIndices[i] = 0;
    }
}

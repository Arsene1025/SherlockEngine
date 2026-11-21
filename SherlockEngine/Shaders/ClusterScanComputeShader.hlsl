#include "ClusterBin.hlsli"

// GPU 클러스터 비닝 4: 상자 3456 개의 개수 → 시작 위치 (배타적 누적합, exclusive prefix sum). 그룹 하나 (1024 스레드).
// 스레드 하나가 이어진 상자 4 개를 맡아 부분합을 내고, 1024 개 부분합을 공유 메모리에서 힐리스-스틸 방식(거리 1, 2, 4 …
// 512 만큼 앞의 값을 더하기 10 번)으로 누적함. 반복 횟수가 상수라 루프 안의 그룹 동기화가 문제없음.
//
// 용량 자르기 = CPU 와 같은 규칙: 상자 순서대로 쌓다가 maxRefs 를 넘는 부분을 버림. 시작 = min(앞의 합, 용량),
// 개수 = min(앞의 합 + 개수, 용량) − 시작. 버린 쌍 수 = max(전체 − 용량, 0). 최대 조명 수는 자르기 전 값 (CPU 와 같음).
static const uint SCAN_THREADS = 1024;
static const uint SCAN_PER_THREAD = 4;   // 1024 × 4 = 4096 ≥ 3456

groupshared uint gsScan[SCAN_THREADS];
groupshared uint gsMax;

[numthreads(SCAN_THREADS, 1, 1)]
void CS_ClusterScan(uint t : SV_GroupIndex)
{
    if (t == 0) gsMax = 0;
    uint counts[SCAN_PER_THREAD];
    uint local = 0;
    uint localMax = 0;
    [unroll]
    for (uint e = 0; e < SCAN_PER_THREAD; ++e)
    {
        const uint c = t * SCAN_PER_THREAD + e;
        counts[e] = c < BIN_CLUSTER_COUNT ? binClusterGrid[c].y : 0;
        local += counts[e];
        localMax = max(localMax, counts[e]);
    }
    gsScan[t] = local;
    GroupMemoryBarrierWithGroupSync();   // gsMax = 0 이 InterlockedMax 보다 먼저
    InterlockedMax(gsMax, localMax);

    //포함적 누적합 (inclusive). 읽기와 쓰기 사이에 동기화 — 같은 단계에서 앞 칸이 먼저 바뀌지 않게
    [unroll]
    for (uint offset = 1; offset < SCAN_THREADS; offset <<= 1)
    {
        //삼항 연산자(t >= offset ? gsScan[t − offset] : 0)는 양쪽을 다 계산해 t < offset 이면 공유 메모리를 범위 밖으로 읽음
        //— D3D11 에서 그룹 공유 메모리의 범위 밖 접근은 정의되지 않은 동작이라 if 로 막음
        uint add = 0;
        if (t >= offset) add = gsScan[t - offset];
        GroupMemoryBarrierWithGroupSync();
        gsScan[t] += add;
        GroupMemoryBarrierWithGroupSync();
    }

    uint prefix = gsScan[t] - local;   // 이 스레드 첫 상자 앞까지의 합 (자르기 전)
    [unroll]
    for (uint e2 = 0; e2 < SCAN_PER_THREAD; ++e2)
    {
        const uint c = t * SCAN_PER_THREAD + e2;
        if (c >= BIN_CLUSTER_COUNT) break;
        const uint start = min(prefix, binMaxRefs);
        const uint end = min(prefix + counts[e2], binMaxRefs);
        binClusterGrid[c] = uint2(start, end - start);
        prefix += counts[e2];
    }

    if (t == SCAN_THREADS - 1)
    {
        const uint total = gsScan[t];
        binStats[STAT_REFS] = min(total, binMaxRefs);
        binStats[STAT_MAX_LIGHTS] = gsMax;
        binStats[STAT_OVERFLOW] = total > binMaxRefs ? total - binMaxRefs : 0;
    }
}

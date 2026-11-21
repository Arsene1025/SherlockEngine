#include "ClusterBin.hlsli"

// GPU 클러스터 비닝 3 (2단계 비닝, 세기): 상자마다 자기 타일의 조명 중 상자와 겹치는 수를 u3[c].y 에 씀.
// 시작 위치(.x)는 다음 단계의 누적합이 채움.
// 타일 통계: 각 타일의 맨 앞 슬라이스(z = 0) 상자를 맡은 스레드가 타일 마스크의 켜진 비트 수를 세어 u1 과 통계 칸에 남김.
[numthreads(64, 1, 1)]
void CS_ClusterCount(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= BIN_CLUSTER_COUNT) return;
    binClusterGrid[id.x] = uint2(0, BinWalkCluster(id.x, false, 0, 0));
    if (binUseTiles != 0 && id.x < BIN_TILE_COUNT)   //z = 0 이면 상자 번호 = 타일 번호
    {
        const uint base = id.x * binTileStride;
        const uint words = BinWordCount();
        uint n = 0;
        [loop]
        for (uint w = 0; w < words; ++w) n += countbits(binTileMasks[base + w]);
        binTileCounts[id.x] = n;
        InterlockedAdd(binStats[STAT_TILE_REFS], n);
        InterlockedMax(binStats[STAT_TILE_MAX_LIGHTS], n);
    }
}

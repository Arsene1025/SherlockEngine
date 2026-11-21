#include "ClusterBin.hlsli"

// GPU 클러스터 비닝 5 (2단계 비닝, 채우기): 상자마다 3 과 같은 검사를 다시 하며 u4 의 자기 구간 [시작, 시작 + 개수) 에 번호를 씀.
// 타일 마스크를 낮은 비트부터 (= 번호 순으로) 읽으므로 상자 목록도 오름차순. 개수가 용량 때문에 잘렸으면 그만큼만 씀.
[numthreads(64, 1, 1)]
void CS_ClusterFill(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= BIN_CLUSTER_COUNT) return;
    const uint2 range = binClusterGrid[id.x];
    if (range.y == 0) return;
    BinWalkCluster(id.x, true, range.x, range.y);
}

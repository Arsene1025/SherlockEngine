#include "ClusterBin.hlsli"

// GPU 클러스터 비닝 1: 조명마다 월드 경계 구 → 이 뷰의 뷰 공간 (CPU 의 XMVector3TransformCoord(중심, view) 와 같음).
// 뒤 단계가 같은 조명을 타일·상자마다 여러 번 읽으므로 변환을 한 번만 해 둠. 스레드 0 은 원자 연산으로 쌓는 통계 칸을 비움
// (다음 디스패치 전에 UAV 배리어가 있음).
[numthreads(64, 1, 1)]
void CS_ClusterView(uint3 id : SV_DispatchThreadID)
{
    if (id.x == 0)
    {
        [unroll]
        for (uint k = 0; k < 8; ++k) binStats[k] = 0;
    }
    if (id.x >= binLightCount) return;
    const float4 b = binLightBounds[id.x];
    //방향광(w < 0)·빛 없음(w = 0)은 반지름만 옮겨 뒤 단계가 건너뛰게 함
    float3 c = float3(0.0f, 0.0f, 0.0f);
    if (b.w > 0.0f)
    {
        const float4 p = float4(b.xyz, 1.0f);
        c = float3(dot(p, binViewColumn[0]), dot(p, binViewColumn[1]), dot(p, binViewColumn[2]));
    }
    binViewBounds[id.x] = float4(c, b.w);
}

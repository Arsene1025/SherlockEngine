// 클러스터드 라이팅의 GPU 쪽 (3단계). 프레임 셋(Layout_Frame)의 t30·t31 과 "픽셀 → 클러스터 번호" 계산.
//
// 클러스터 = 화면을 CLUSTER_X × CLUSTER_Y 타일로, 깊이를 CLUSTER_Z 슬라이스로 나눈 뷰 공간 상자 하나.
// 프레임마다 뷰마다 조명의 경계 구와 상자를 맞대 보고 상자마다 닿는 지역광 번호 목록을 만듦. 기본은 컴퓨트 비닝
// (ClusterBin.hlsli 의 다섯 패스, Graphics/RendererClusterBin.cpp) 이 t30·t31 버퍼를 직접 채움. --cluster-gpu=0 이거나 컴퓨트를
// 쓸 수 없으면 CPU 비닝 (Graphics/RendererClusters.cpp 의 BuildClusters) 이 같은 형식으로 만들어 올림. 셰이더는 자기 픽셀의
// 상자를 찾아 그 목록만 돎.
//
//   t30 clusterGrid[c]   = (clusterLightIndices 의 시작 위치, 개수)
//   t31 clusterLightIndices = 조명 번호 (t15 lights 의 인덱스). 번호는 상자 안에서 오름차순 — 전체 순회와 덧셈 순서가 같아짐
//
// 클러스터 번호 c = x + CLUSTER_X · (y + CLUSTER_Y · z). x 는 왼→오, y 는 위→아래 (SV_Position 과 같은 방향).
#ifndef SHERLOCK_CLUSTERS_HLSLI
#define SHERLOCK_CLUSTERS_HLSLI

#include "Common.hlsli"

static const uint CLUSTER_X = 16;   // Graphics/ShaderConstants.h 의 kClusterGridX/Y/Z 와 같아야 함
static const uint CLUSTER_Y = 9;
static const uint CLUSTER_Z = 24;

StructuredBuffer<uint2> clusterGrid : register(t30);
StructuredBuffer<uint> clusterLightIndices : register(t31);

// 뷰 공간 z (카메라 앞쪽 거리) → 슬라이스 0 .. CLUSTER_Z − 1.
// 원근: 지수 분할 slice = log2(z) · S / log2(f/n) − S · log2(n) / log2(f/n). 가까울수록 얇은 슬라이스 — 원근 투영에서
// 화면에 보이는 크기가 거리에 반비례하므로, 슬라이스가 화면에서 비슷한 "두께"가 되게 함. 직교: 균등 분할.
// near 앞·far 뒤는 첫·마지막 슬라이스로 붙임 (CPU·GPU 비닝의 상자도 같은 범위).
uint ClusterSlice(float viewZ)
{
    const float f = clusterParams.y > 0.5f ? log2(max(viewZ, 1.0e-4f)) : viewZ;
    const float slice = floor(f * clusterDepth.x + clusterDepth.y);
    return (uint)clamp(slice, 0.0f, (float)(CLUSTER_Z - 1));
}

// 화면 픽셀(SV_Position.xy) + 뷰 z → 클러스터 번호.
uint ClusterIndex(float2 pixel, float viewZ)
{
    const uint2 tile = min((uint2)(pixel * screenParams.zw * float2(CLUSTER_X, CLUSTER_Y)), uint2(CLUSTER_X - 1, CLUSTER_Y - 1));
    return tile.x + CLUSTER_X * (tile.y + CLUSTER_Y * ClusterSlice(viewZ));
}

#endif // SHERLOCK_CLUSTERS_HLSLI

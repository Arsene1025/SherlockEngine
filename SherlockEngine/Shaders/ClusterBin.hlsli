// GPU 클러스터 비닝 (컴퓨트). CPU 비닝(Graphics/RendererClusters.cpp 의 BuildClusters)과 같은 결과 형식 — 프레임 셋의
// t30 (상자마다 (시작, 개수)) 과 t31 (조명 번호 목록) — 을 컴퓨트 셰이더 다섯 개로 만듦. 설계와 읽는 순서:
// docs/plan/gpu-cluster-binning-record.html.
//
//   1. CS_ClusterView   조명마다: 월드 경계 구 (t7) → 이 뷰의 뷰 공간 (u0)
//   2. CS_ClusterTile   (타일 144 개 × 조명 32 개 워드)마다: 타일 절두체(옆면 4 개)와 겹치는 지역광을 u2 비트마스크에 (1단계 비닝)
//   3. CS_ClusterCount  상자 3456 개마다: 자기 타일 마스크의 조명 중 상자(AABB)와 겹치는 수 → u3[c].y (2단계 비닝)
//   4. CS_ClusterScan   그룹 하나: 개수의 누적합 → u3[c] = (시작, 개수). 용량을 넘는 쌍은 CPU 와 같은 규칙으로 잘라 냄
//   5. CS_ClusterFill   상자마다: 3 과 같은 검사를 다시 하며 번호를 u4 의 자기 구간에 채움
//
// 순서 불변식: 상자 안 번호는 오름차순 (CPU 경로·전체 순회와 덧셈 순서가 같음). 스레드 하나가 상자 하나를 맡아 타일
// 마스크를 낮은 비트부터 (= 번호 순으로) 차례로 돌기 때문에 원자 연산 없이 순서가 지켜짐. 그룹 공유 메모리로 나눠 도는 것보다
// 느리지만, 반복 횟수가 버퍼에서 읽은 값인 루프 안에 그룹 동기화를 두지 않아도 됨 (fxc SM 5.0 의 X3663 제약).
#ifndef SHERLOCK_CLUSTER_BIN_HLSLI
#define SHERLOCK_CLUSTER_BIN_HLSLI

static const uint BIN_CLUSTER_X = 16;   // Graphics/ShaderConstants.h 의 kClusterGridX/Y/Z 와 같아야 함 (Clusters.hlsli 와도)
static const uint BIN_CLUSTER_Y = 9;
static const uint BIN_CLUSTER_Z = 24;
static const uint BIN_TILE_COUNT = BIN_CLUSTER_X * BIN_CLUSTER_Y;          // 144
static const uint BIN_CLUSTER_COUNT = BIN_TILE_COUNT * BIN_CLUSTER_Z;       // 3456

// u5 통계 칸 (ShaderConstants.h 의 ClusterStat_*)
static const uint STAT_REFS = 0;           // t31 에 들어간 쌍 수 (잘린 뒤)
static const uint STAT_MAX_LIGHTS = 1;     // 한 상자의 최대 조명 수 (자르기 전)
static const uint STAT_OVERFLOW = 2;       // 용량이 모자라 버린 쌍 수
static const uint STAT_TILE_REFS = 3;      // 타일 마스크에 켜진 쌍 수 (1단계)
static const uint STAT_TILE_MAX_LIGHTS = 4;

// b13: 뷰 하나의 값 (C++ ClusterBinConstants)
cbuffer ClusterBin : register(b13)
{
    float4 binViewColumn[3];   // 월드 → 뷰. x = dot(float4(p, 1), binViewColumn[0])
    float4 binProjScale;       // (P._11, P._22, P._31, P._32)
    float4 binProjOffset;      // (P._41, P._42, near, far)
    float4 binSliceZ[7];       // 슬라이스 경계 z_0 .. z_24
    uint binLightCount;
    uint binTileStride;        // u2 에서 타일 하나의 워드 수 (= ⌈조명 용량 / 32⌉)
    uint binPerspective;       // 1 원근, 0 직교
    uint binMaxRefs;           // u4 용량
    uint binUseTiles;          // 1 = 2단계 (타일 마스크), 0 = 1단계 (조명 전체 — 비교 측정용)
    uint3 binPadding;
}

StructuredBuffer<float4> binLightBounds : register(t7);            // 월드 경계 구 (xyz 중심, w 반지름. w ≤ 0 = 방향광·빛 없음)
RWStructuredBuffer<float4> binViewBounds : register(u0);            // 뷰 공간 경계 구 (같은 번호)
RWStructuredBuffer<uint> binTileCounts : register(u1);             // 타일마다 켜진 비트 수 (통계·디버그용, Count 가 씀)
RWStructuredBuffer<uint> binTileMasks : register(u2);              // 타일마다 조명 비트마스크. 비트 j = 조명 w·32 + j
RWStructuredBuffer<uint2> binClusterGrid : register(u3);            // (시작, 개수) — 그리기 패스에서는 t30
RWStructuredBuffer<uint> binClusterLights : register(u4);           // 조명 번호 — 그리기 패스에서는 t31
RWStructuredBuffer<uint> binStats : register(u5);

// 이번 프레임 조명 수를 덮는 마스크 워드 수. Tile 이 쓰는 범위와 Count·Fill 이 읽는 범위가 같아야 함 (지난 프레임 비트가 남지 않게).
uint BinWordCount()
{
    return (binLightCount + 31) / 32;
}

float BinSliceZ(uint k)
{
    return binSliceZ[k >> 2][k & 3];
}

// NDC → 뷰 공간 x·y (CPU BuildClusters 의 toViewX/Y 와 같은 식). 원근 x = (ndc − P31) · z / P11, 직교 x = (ndc − P41) / P11.
float BinToViewX(float ndc, float z)
{
    return binPerspective != 0 ? (ndc - binProjScale.z) * z / binProjScale.x : (ndc - binProjOffset.x) / binProjScale.x;
}
float BinToViewY(float ndc, float z)
{
    return binPerspective != 0 ? (ndc - binProjScale.w) * z / binProjScale.y : (ndc - binProjOffset.y) / binProjScale.y;
}

// 타일의 NDC 사각형. 타일 y 는 위→아래 (SV_Position 방향) 라 NDC y 는 1 → −1.
void BinTileNdc(uint2 tile, out float nx0, out float nx1, out float ny0, out float ny1)
{
    nx0 = -1.0f + 2.0f * (float)tile.x / BIN_CLUSTER_X;
    nx1 = -1.0f + 2.0f * (float)(tile.x + 1) / BIN_CLUSTER_X;
    ny0 = 1.0f - 2.0f * (float)tile.y / BIN_CLUSTER_Y;          // 위
    ny1 = 1.0f - 2.0f * (float)(tile.y + 1) / BIN_CLUSTER_Y;    // 아래
}

// 상자 c 의 뷰 공간 AABB: 타일 사각형을 슬라이스 앞·뒤 z 에서 뷰 공간으로 되돌린 꼭짓점 8 개를 감쌈 (CPU 와 같은 식).
void BinClusterBox(uint3 cell, out float3 mn, out float3 mx)
{
    float nx0, nx1, ny0, ny1;
    BinTileNdc(cell.xy, nx0, nx1, ny0, ny1);
    const float z0 = BinSliceZ(cell.z);
    const float z1 = BinSliceZ(cell.z + 1);
    const float x00 = BinToViewX(nx0, z0), x10 = BinToViewX(nx1, z0), x01 = BinToViewX(nx0, z1), x11 = BinToViewX(nx1, z1);
    const float y00 = BinToViewY(ny0, z0), y10 = BinToViewY(ny1, z0), y01 = BinToViewY(ny0, z1), y11 = BinToViewY(ny1, z1);
    mn = float3(min(min(x00, x10), min(x01, x11)), min(min(y00, y10), min(y01, y11)), z0);
    mx = float3(max(max(x00, x10), max(x01, x11)), max(max(y00, y10), max(y01, y11)), z1);
}

// 구와 AABB 의 겹침: 상자 안에서 구 중심에 가장 가까운 점까지의 거리² ≤ r² (CPU SphereIntersectsBox 와 같음).
bool BinSphereIntersectsBox(float3 c, float r, float3 mn, float3 mx)
{
    const float3 d = max(mn - c, 0.0f) + max(c - mx, 0.0f);
    return dot(d, d) <= r * r;
}

// 상자 c 를 맡은 스레드: 조명을 번호 순으로 돌며 상자와 겹치는 것을 셈. write 면 u4[start + n] 에 채우되 limit (잘린 개수)
// 까지만. 3 (Count) 과 5 (Fill) 가 같은 함수를 씀 — 검사가 같아야 개수와 채운 칸 수가 맞음.
//   2단계: 자기 타일 마스크의 켜진 비트만 (낮은 비트부터 = 번호 순). 타일 절두체로 이미 거른 조명이라 방향광은 없음.
//   1단계 (비교용, binUseTiles = 0): 조명 전체.
uint BinWalkCluster(uint c, bool write, uint start, uint limit)
{
    const uint3 cell = uint3(c % BIN_CLUSTER_X, (c / BIN_CLUSTER_X) % BIN_CLUSTER_Y, c / BIN_TILE_COUNT);
    float3 mn, mx;
    BinClusterBox(cell, mn, mx);
    uint n = 0;
    if (binUseTiles != 0)
    {
        const uint base = (cell.x + BIN_CLUSTER_X * cell.y) * binTileStride;
        const uint words = BinWordCount();
        [loop]
        for (uint w = 0; w < words; ++w)
        {
            if (write && n >= limit) break;
            uint bits = binTileMasks[base + w];
            [loop]
            while (bits != 0)
            {
                if (write && n >= limit) break;
                const uint light = w * 32 + firstbitlow(bits);
                bits &= bits - 1;   //가장 낮은 켜진 비트를 끔
                const float4 s = binViewBounds[light];
                if (!BinSphereIntersectsBox(s.xyz, s.w, mn, mx)) continue;
                if (write) binClusterLights[start + n] = light;
                ++n;
            }
        }
    }
    else
    {
        [loop]
        for (uint light = 0; light < binLightCount; ++light)
        {
            if (write && n >= limit) break;
            const float4 s = binViewBounds[light];
            if (s.w <= 0.0f) continue;   //방향광·빛 없음
            if (!BinSphereIntersectsBox(s.xyz, s.w, mn, mx)) continue;
            if (write) binClusterLights[start + n] = light;
            ++n;
        }
    }
    return n;
}

#endif // SHERLOCK_CLUSTER_BIN_HLSLI

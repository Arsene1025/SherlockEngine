#include "ClusterBin.hlsli"

// GPU 클러스터 비닝 2 (1단계 비닝): 화면 타일마다 그 타일의 절두체와 겹치는 지역광을 비트마스크로 u2 에 표시함.
// 스레드 하나가 (타일, 조명 32 개 = 워드 하나) 를 맡아 32 개를 검사하고 워드를 통째로 씀 — 원자 연산·그룹 동기화가 없고,
// 비트 위치가 곧 조명 번호라 다음 단계가 낮은 비트부터 읽으면 오름차순이 저절로 지켜짐. 타일 t 의 워드는 u2 의
// [t · stride, t · stride + ⌈조명 수 / 32⌉) 이고, 이번 프레임에 읽는 워드는 모두 여기서 씀 (조명 수 밖의 비트는 0).
//
// 처음에는 스레드 하나가 타일 하나를 맡아 조명 전체를 차례로 돌며 번호 목록을 만들었음 — 스레드가 144 개뿐이라 촛불 1만 개
// 씬에서 이 단계만 1.6 ms. 지금은 스레드 144 × ⌈N / 32⌉ 개 (1만 개면 약 4.5만).
//
// 원근: 타일 절두체 = 눈을 지나는 옆면 평면 4 개. 왼쪽 면은 x_ndc ≥ nx0 ⇔ P11·x + (P31 − nx0)·z ≥ 0 (z > 0) 이고, 구는 모든 면에서
// 부호 거리 ≥ −r 이면 겹친다고 봄 (보수적 — 모서리 바깥에서 통과하는 구가 있음). 타일 AABB 로 검사하면 near~far 전체를 감싸
// 상자가 절두체의 절반 가까이 되므로 거의 거르지 못함. 직교: 옆면이 축에 평행하므로 x·y 구간.
// 깊이: 구가 [near, far] 밖이면 버림 (CPU 와 같음).
[numthreads(64, 1, 1)]
void CS_ClusterTile(uint3 id : SV_DispatchThreadID)
{
    const uint word = id.x;
    const uint tile = id.y;   //Dispatch(⌈워드 수 / 64⌉, 144, 1)
    if (word >= BinWordCount() || tile >= BIN_TILE_COUNT) return;
    float nx0, nx1, ny0, ny1;
    BinTileNdc(uint2(tile % BIN_CLUSTER_X, tile / BIN_CLUSTER_X), nx0, nx1, ny0, ny1);
    const float p11 = binProjScale.x, p22 = binProjScale.y, p31 = binProjScale.z, p32 = binProjScale.w;
    const float nearZ = binProjOffset.z, farZ = binProjOffset.w;
    const bool perspective = binPerspective != 0;
    //원근의 옆면 4 개 (안쪽이 +). 왼·오른쪽, 위(ny0)·아래(ny1)
    const float3 left = normalize(float3(p11, 0.0f, p31 - nx0));
    const float3 right = normalize(float3(-p11, 0.0f, nx1 - p31));
    const float3 top = normalize(float3(0.0f, -p22, ny0 - p32));
    const float3 bottom = normalize(float3(0.0f, p22, p32 - ny1));
    //직교의 x·y 구간 (뷰 공간)
    const float xMin = (nx0 - binProjOffset.x) / p11, xMax = (nx1 - binProjOffset.x) / p11;
    const float yMin = (ny1 - binProjOffset.y) / p22, yMax = (ny0 - binProjOffset.y) / p22;

    uint bits = 0;
    [loop]
    for (uint b = 0; b < 32; ++b)
    {
        const uint i = word * 32 + b;
        if (i >= binLightCount) break;
        const float4 s = binViewBounds[i];
        //방향광·빛 없음은 셰이더가 따로 다 돎
        if (s.w <= 0.0f) continue;
        if (s.z + s.w < nearZ || s.z - s.w > farZ) continue;
        bool inside;
        if (perspective)
        {
            inside = dot(left, s.xyz) >= -s.w && dot(right, s.xyz) >= -s.w && dot(top, s.xyz) >= -s.w && dot(bottom, s.xyz) >= -s.w;
        }
        else
        {
            inside = s.x + s.w >= xMin && s.x - s.w <= xMax && s.y + s.w >= yMin && s.y - s.w <= yMax;
        }
        if (inside) bits |= 1u << b;
    }
    binTileMasks[tile * binTileStride + word] = bits;
}

#define RAIN_DRAW
#include "Rain.hlsli"

// 튐 (docs/plan/rain-plan.html 3단계). 빗줄기처럼 정점 버퍼 없이 Draw(튐 수 × 6). 튐 하나 = 카메라 주변 격자의 칸 하나:
//   - 칸·자리·순간은 물결과 같은 RainImpactInCell (Rain.hlsli). 빗방울이 떨어진 순간부터 splashPeriod 동안 그 자리에서 터짐 —
//     같은 자리에서 물결 링이 퍼지기 시작함. 이번 주기에 떨어지지 않은 칸은 그리지 않음 (rainSurface.z 비율만 떨어짐).
//   - 튐 번호 → (겹, 칸): 겹마다 rainSplashGrid² 칸, 카메라 xz 가 든 칸이 가운데. 카메라가 움직여도 칸은 월드에 고정.
//   - y : 그 xz 의 차폐 맵 높이 = 비가 처음 닿는 면 (지붕 위·열린 바닥). 맵 밖이거나 아무것도 없으면 그리지 않음.
// 쿼드는 세로축이 월드 위 (+y) 이고 카메라 쪽을 보는 원통형 빌보드 — 아래 끝이 면에 닿음. 모양은 PS 가 그림 (퍼지는 왕관).

static const float2 kSplashCorners[6] =
{
    float2(-1.0f, 0.0f), float2(-1.0f, 1.0f), float2(1.0f, 1.0f),
    float2(-1.0f, 0.0f), float2(1.0f, 1.0f), float2(1.0f, 0.0f),
};

RainVSOutput CulledSplash()
{
    RainVSOutput output;
    output.position = float4(0.0f, 0.0f, -1.0f, 1.0f);
    output.quad = float2(0.0f, 0.0f);
    output.color = float3(0.0f, 0.0f, 0.0f);
    output.extra = float2(0.0f, 0.0f);
    return output;
}

RainVSOutput VS_RainSplash(uint vertexId : SV_VertexID)
{
    const uint splash = vertexId / 6;
    const float2 corner = kSplashCorners[vertexId % 6];

    // 튐 번호 → 겹·칸
    const uint grid = rainSplashGrid;
    const uint perLayer = grid * grid;
    const int layer = splash >= perLayer ? 1 : 0;
    const uint index = splash - (uint)layer * perLayer;
    const float2 layerOffset = RainImpactLayerOffset(layer);
    const int2 cameraCell = (int2)floor(cameraPosition.xz / kRainImpactCell + layerOffset);
    const int2 cell = cameraCell - (int)(grid / 2) + int2(index % grid, index / grid);

    const RainImpact impact = RainImpactInCell(cell, layer);
    if (!impact.active) return CulledSplash();
    const float age = impact.phase / kRainImpactRate;   // 떨어진 뒤 지난 시간 (초)
    if (age >= rainSplashPeriod) return CulledSplash();
    const float progress = age / rainSplashPeriod;      // 0..1
    const uint hc = impact.hash;
    const float2 xz = impact.xz;

    // 카메라에서 splashRadius 쪽으로 갈수록 흐려짐
    const float distanceFade = saturate((1.0f - length(xz - cameraPosition.xz) / rainSplashRadius) / 0.25f);
    if (distanceFade <= 0.0f) return CulledSplash();

    const float surface = RainOccluderHeight(xz);
    if (surface < -1.0e29f) return CulledSplash();                              // 맵 밖 또는 아무것도 없음
    if (abs(surface - cameraPosition.y) > rainBoxSize.y) return CulledSplash();   // 빗줄기 상자보다 멀리 위·아래

    const float3 base = float3(xz.x, surface + 0.01f, xz.y);
    float3 toCamera = cameraPosition - base;
    toCamera.y = 0.0f;
    const float3 right = dot(toCamera, toCamera) > 1.0e-6f ? normalize(cross(float3(0.0f, 1.0f, 0.0f), toCamera)) : float3(1.0f, 0.0f, 0.0f);
    const float size = rainSplashSize * (0.7f + 0.6f * RainHash01(hc ^ 0xc2b2ae35u));
    const float3 world = base + right * (corner.x * size) + float3(0.0f, corner.y * size, 0.0f);

    const float4 viewPosition = mul(float4(world, 1.0f), view);
    if (viewPosition.z <= 0.02f) return CulledSplash();

    RainVSOutput output;
    output.position = mul(viewPosition, proj);
    output.quad = corner;
    output.color = rainColor * (2.0f * distanceFade * RainEnvironmentScale());
    output.extra = float2(viewPosition.z, progress);
    return output;
}

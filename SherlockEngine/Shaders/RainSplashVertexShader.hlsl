#define RAIN_DRAW
#include "Rain.hlsli"

// 튐 (docs/plan/rain-plan.html 3단계). 빗줄기처럼 정점 버퍼 없이 Draw(튐 수 × 6). 튐 하나는 splashPeriod 마다 다시 태어나고,
// 태어날 때마다 (주기 번호를 해시에 섞어) 새 자리를 고름:
//   - xz: 카메라 중심 정사각형 안의 월드 고정 격자 점 (빗방울과 같은 감기). 한 주기 동안은 카메라가 움직여도 그 자리에 있음.
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

    // 주기: 튐마다 시작 위상이 달라 한꺼번에 태어나지 않음
    const uint h = RainHash(splash * 2654435761u ^ RainHash(rainSeed + 0x9e3779b9u));
    const float cyclePosition = rainSplashTime / rainSplashPeriod + RainHash01(h);
    const float cycle = floor(cyclePosition);
    const float progress = cyclePosition - cycle;   // 0..1
    const uint hc = RainHash(h ^ RainHash((uint)cycle));

    // xz: 한 변 2 × splashRadius 의 월드 고정 격자 (빗방울과 같은 감기)
    const float span = 2.0f * rainSplashRadius;
    const float2 boxMin = cameraPosition.xz - rainSplashRadius;
    const float2 anchor = float2(RainHash01(hc), RainHash01(hc ^ 0x85ebca6bu)) * span;
    const float2 local = frac((anchor - boxMin) / span);
    const float2 xz = boxMin + local * span;

    const float surface = RainOccluderHeight(xz);
    if (surface < -1.0e29f) return CulledSplash();                              // 맵 밖 또는 아무것도 없음
    if (abs(surface - cameraPosition.y) > rainBoxSize.y) return CulledSplash();   // 빗줄기 상자보다 멀리 위·아래

    // 카메라에서 멀수록 (정사각형 → 원) 흐려짐
    const float distanceFade = saturate((1.0f - length(local * 2.0f - 1.0f)) / 0.25f);
    if (distanceFade <= 0.0f) return CulledSplash();

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

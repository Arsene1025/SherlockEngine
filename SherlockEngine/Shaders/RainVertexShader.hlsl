#define RAIN_DRAW
#include "Rain.hlsli"

// 빗줄기 (docs/plan/rain-plan.html 1단계). 정점 버퍼 없이 Draw(빗방울 수 × 6) — SV_VertexID / 6 이 빗방울 번호, % 6 이 쿼드 모서리.
// 상태 없는 절차적 파티클: 빗방울 i 의 위치 = 해시로 정한 시작점 + 속도 × 시간, 카메라 중심 상자 안으로 frac 으로 감음.
//   - 시작점은 월드에 고정된 격자 (상자 크기 주기) 라 카메라가 움직여도 빗방울이 따라 미끄러지지 않음. 상자 가장자리에서만 반대편으로 넘어감
//     (가장자리 10% 에서 흐려지므로 보이지 않음).
//   - 속도 × 시간은 CPU 가 축마다 상자 크기로 감아 rainFallOffset 으로 줌 (float 정밀도, 바람 방향 감기).
//   - 머리가 차폐 맵 아래 (지붕 밑) 면 그 빗방울은 그리지 않음 (클립 공간 밖으로 보냄).
// 빗줄기 = 머리에서 속도 반대쪽으로 streakLength 만큼 늘인 선. 화면을 향한 쿼드로 펴고, 폭은 최소 1 픽셀로 키운 만큼 어둡게 함.

static const float2 kStreakCorners[6] =
{
    float2(-1.0f, 0.0f), float2(-1.0f, 1.0f), float2(1.0f, 1.0f),
    float2(-1.0f, 0.0f), float2(1.0f, 1.0f), float2(1.0f, 0.0f),
};

RainVSOutput CulledRain()
{
    RainVSOutput output;
    output.position = float4(0.0f, 0.0f, -1.0f, 1.0f);   // z < 0 → 잘림
    output.quad = float2(0.0f, 0.0f);
    output.color = float3(0.0f, 0.0f, 0.0f);
    output.extra = float2(0.0f, 0.0f);
    return output;
}

RainVSOutput VS_Rain(uint vertexId : SV_VertexID)
{
    const uint drop = vertexId / 6;
    const float2 corner = kStreakCorners[vertexId % 6];

    // 상자: 카메라 중심, 가로 boxSize.xz, 세로는 카메라 아래 boxBelow 몫
    const float3 boxMin = cameraPosition - float3(0.5f * rainBoxSize.x, rainBoxBelow * rainBoxSize.y, 0.5f * rainBoxSize.z);
    const float3 anchor = RainHash3(drop, rainSeed) * rainBoxSize;   // 월드 격자 안의 시작점
    const float3 local = frac((anchor + rainFallOffset - boxMin) / rainBoxSize);   // 상자 안 0..1
    const float3 head = boxMin + local * rainBoxSize;

    if (!RainExposed(head)) return CulledRain();

    // 상자 가장자리 10% 에서 흐려짐 (감기는 순간이 보이지 않게)
    const float3 edge = saturate(min(local, 1.0f - local) / 0.1f);
    const float edgeFade = edge.x * edge.y * edge.z;

    const float3 direction = normalize(rainVelocity);
    const float3 tail = head - direction * rainStreakLength;
    const float4 headView = mul(float4(head, 1.0f), view);
    const float4 tailView = mul(float4(tail, 1.0f), view);
    const float viewZ = min(headView.z, tailView.z);
    if (viewZ <= 0.02f) return CulledRain();   // 카메라 뒤·바로 앞

    // 화면에서 선에 수직인 방향 (뷰 공간 xy). 선이 화면을 정면으로 뚫으면 (위·아래를 볼 때) 아무 방향
    float2 axis = tailView.xy / max(tailView.z, 0.01f) - headView.xy / max(headView.z, 0.01f);
    axis = dot(axis, axis) > 1.0e-10f ? normalize(axis) : float2(0.0f, 1.0f);
    const float2 side = float2(-axis.y, axis.x);

    const float pixel = RainPixelWorldSize(viewZ);
    const float width = max(rainStreakWidth, pixel);
    const float widthScale = rainStreakWidth / width;   // 1 픽셀로 키운 만큼 어둡게 (화면에 들어가는 빛의 양을 유지)
    const float nearFade = smoothstep(0.0f, rainNearFade, viewZ - 0.05f);

    float4 viewPosition = lerp(headView, tailView, corner.y);
    viewPosition.xy += side * (corner.x * 0.5f * width);

    RainVSOutput output;
    output.position = mul(viewPosition, proj);
    output.quad = corner;
    output.color = rainColor * (edgeFade * widthScale * nearFade * RainEnvironmentScale());
    output.extra = float2(viewPosition.z, 0.0f);
    return output;
}

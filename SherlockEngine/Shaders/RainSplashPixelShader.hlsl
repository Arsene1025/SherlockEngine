#define RAIN_DRAW
#include "Rain.hlsli"

// 튐 PS: 면에서 퍼지며 솟는 왕관. quad.x = 가로 −1..1, quad.y = 위로 0..1 (0 이 면). 진행도 u 에 따라 반지름이 커지고, 각도마다
// 가시 (cos 6θ) 가 서며, (1 − u)² 로 사라짐. 아래 반쪽은 면 아래라 그리지 않음.
float4 PS_RainSplash(RainVSOutput input) : SV_TARGET
{
    const float u = input.extra.y;
    const float2 p = float2(input.quad.x, input.quad.y);
    const float r = length(p);
    const float radius = 0.25f + 0.65f * sqrt(u);
    const float thickness = 0.12f * (1.0f - 0.5f * u);
    float ring = saturate(1.0f - abs(r - radius) / thickness);
    const float angle = atan2(p.y, p.x);   // 0..π (위 반쪽)
    const float spikes = 0.55f + 0.45f * cos(angle * 6.0f + u * 2.0f);
    ring *= spikes;
    const float fade = (1.0f - u) * (1.0f - u);
    const float soft = RainSoftFade(input.position, input.extra.x, 0.15f * rainSoftDistance);
    return float4(input.color * (ring * fade * soft), 0.0f);
}

#define RAIN_DRAW
#include "Rain.hlsli"

// 빗줄기 PS: 가로 단면은 가운데가 밝은 포물선, 세로는 머리 (아래쪽 끝) 가 밝고 꼬리로 갈수록 흐려짐. 장면과 만나는 곳은 소프트 페이드.
// 가산 블렌드 (파티클 PSO 와 같은 One / InvSrcAlpha 에 알파 0) — 정렬이 필요 없음.
float4 PS_Rain(RainVSOutput input) : SV_TARGET
{
    const float across = 1.0f - input.quad.x * input.quad.x;
    const float along = lerp(1.0f, 0.2f, input.quad.y);
    const float soft = RainSoftFade(input.position, input.extra.x, rainSoftDistance);
    return float4(input.color * (across * along * soft), 0.0f);
}

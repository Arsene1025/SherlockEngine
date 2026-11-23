// 속도 버퍼 (docs/plan/velocity-motionblur-taa-plan.html 1단계). 화면 공간 속도 = uv(이번 프레임) − uv(지난 프레임),
// UV 단위 (v 아래로). 받는 쪽은 uv(지난) = uv − velocity 로 지난 프레임 위치를 찾음. 지터는 들어가지 않음 —
// 두 클립 좌표 모두 b0 의 지터 없는 행렬(unjitteredViewProj · prevUnjitteredViewProj)로 만듦.
#ifndef SHERLOCK_VELOCITY_HLSLI
#define SHERLOCK_VELOCITY_HLSLI

#include "Common.hlsli"

// 클립 좌표 두 개 → UV 속도. NDC 는 y 가 위로 +1 이고 UV 는 아래로 +1 이라 y 부호를 뒤집음 (NDC → UV 는 × (0.5, −0.5)).
// 지난 프레임에 이 점이 카메라 평면 뒤(w ≤ 0)였으면 투영이 뒤집히거나 무한대가 됨 (한 프레임에 크게 뒤로 물러남, 뒤에서 들어온 물체,
// 카메라 컷). 그런 점은 지난 위치를 알 수 없으므로 0 으로 둠. 결과도 ±2 UV (화면 두 장) 로 자름 — FP16 무한대·NaN 이 후처리로
// 번지지 않게.
float2 VelocityUV(float4 currClip, float4 prevClip)
{
    const bool valid = currClip.w > 1e-4f && prevClip.w > 1e-4f;
    const float2 curr = currClip.xy / max(currClip.w, 1e-4f);
    const float2 prev = prevClip.xy / max(prevClip.w, 1e-4f);
    return valid ? clamp((curr - prev) * float2(0.5f, -0.5f), -2.0f, 2.0f) : float2(0.0f, 0.0f);
}

// 깊이 재투영으로 구한 "카메라만의" 속도: 표면이 월드에 정지해 있다고 보고 지난 프레임 viewProj 로 다시 투영함.
// 디버그 뷰 17(속도 오차)이 속도 버퍼와 비교함. 이번 프레임 쪽도 복원한 월드 위치를 지터 없는 행렬로 투영함 —
// 픽셀 uv 를 그대로 쓰면 TAA 지터가 남은 프레임에서 최대 0.5 픽셀 오차가 생김. pixel 은 SV_Position.xy.
float2 CameraVelocityUV(float2 pixel, float deviceDepth)
{
    const float4 world = float4(ReconstructWorldPosition(pixel, deviceDepth), 1.0f);
    return VelocityUV(mul(world, unjitteredViewProj), mul(world, prevUnjitteredViewProj));
}

#endif // SHERLOCK_VELOCITY_HLSLI

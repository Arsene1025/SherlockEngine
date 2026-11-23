#include "Temporal.hlsli"

// 디버그 뷰 16·17 (속도 버퍼 검증). 씬 패스 뒤 hdr 를 이 색으로 덮어씀 — 톤 패스는 디버그 뷰를 노출·ACES 없이 그대로 냄.
//   16 속도: 방향 = 색상 (오른쪽 빨강 → 아래 초록 → 왼쪽 청록 → 위 보라), 크기 = 밝기 (16 픽셀에서 최대). 정지 화면은 검정.
//   17 속도 오차: |속도 버퍼 − 깊이 재투영으로 구한 카메라 속도| (픽셀) 에서 허용치를 뺀 나머지 × 10 (0.1 픽셀 넘으면 흰색).
//      정지한 물체는 두 값이 같아야 하므로 검정이고, 움직이는 오브젝트만 밝게 나옴. 허용치 = FP16 양자화 (R16G16_FLOAT 는 값의
//      2^-11 단위로 반올림 → 두 성분 합쳐 |v| × 약 7e-4) + 0.01 픽셀. 허용치 없이 × 100 하면 1°/프레임 회전(약 6 픽셀)에서도
//      반올림 무늬가 화면을 덮음. 하늘(깊이 1)은 무한히 먼 방향이라 재투영과 값이 달라 비교에서 빼고 어두운 파랑.
float3 HueToRgb(float hue)
{
    const float3 rgb = saturate(abs(frac(hue + float3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) - 1.0f);
    return rgb;
}

float4 PS_VelocityDebug(float4 position : SV_POSITION) : SV_TARGET
{
    const int2 pixel = int2(position.xy);
    const float2 velocity = velocityTexture.Load(int3(pixel, 0));
    const float2 velocityPixels = velocity * screenParams.xy;
    if ((int)debugParams.x == 17)
    {
        const float deviceDepth = temporalDepth.Load(int3(pixel, 0));
        if (deviceDepth >= 1.0f) return float4(0.0f, 0.0f, 0.15f, 1.0f);
        const float2 cameraVelocity = CameraVelocityUV(position.xy, deviceDepth);
        const float error = length((velocity - cameraVelocity) * screenParams.xy);
        const float tolerance = 1e-3f * length(cameraVelocity * screenParams.xy) + 0.01f;
        return float4(saturate((error - tolerance) * 10.0f).xxx, 1.0f);
    }
    const float magnitude = length(velocityPixels);
    if (magnitude < 1e-4f) return float4(0.0f, 0.0f, 0.0f, 1.0f);
    const float hue = atan2(velocityPixels.y, velocityPixels.x) / (2.0f * PI) + 1.0f;
    return float4(HueToRgb(hue) * saturate(magnitude / 16.0f), 1.0f);
}

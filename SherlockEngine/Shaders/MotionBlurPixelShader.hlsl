#include "Temporal.hlsli"

// 모션 블러 (2단계, docs/plan/velocity-motionblur-taa-plan.html). 입력 = 시간 셋 (t13 hdr, t23 깊이, t7 속도), 출력 = 스크래치.
// 속도 버퍼를 따라 픽셀 양쪽으로 샘플을 늘어놓고 McGuire 2012 ("A Reconstruction Filter for Plausible Motion Blur") 의 샘플별
// 가중치로 섞음. 가중치가 깊이 앞뒤와 각 샘플의 자기 속도를 보므로, 움직이는 전경은 배경 위로 번지지만 정지한 배경이
// 전경 위로 번지지는 않음 (실루엣 바깥으로 번지는 타일 최대 속도는 넣지 않음 — 계획 2-3).
//
// b4 PostParams 칸: blur.x = 셔터 배율, blur.y = 최대 길이 (픽셀, 출력 높이 반영), blur.z = 샘플 수.

// 이 픽셀의 번지는 벡터 (픽셀). 한 프레임 이동량 × 셔터 배율을 최대 길이로 자름.
float2 BlurVector(int2 pixel)
{
    const float2 v = LoadVelocityPixels(pixel) * postBlur.x;
    const float len = length(v);
    const float scale = len > postBlur.y ? postBlur.y / len : 1.0f;
    return (len < 1e6f) ? v * scale : float2(0.0f, 0.0f);   // 무한대·NaN 이면 0 (속도 버퍼가 이미 막지만 한 번 더 — NaN 비교는 거짓)
}

// za 가 zb 보다 앞(가까움)이면 1, 뒤면 0. 경계는 거리에 비례한 폭으로 부드럽게 (먼 곳은 깊이 정밀도가 낮음).
float SoftFront(float za, float zb)
{
    const float extent = 0.02f + 0.02f * min(za, zb);
    return saturate(1.0f - (za - zb) / extent);
}

// 길이 v 로 번지는 점이 거리 d 에 주는 기여: 원뿔 (멀수록 줄어 끝에서 0), 원기둥 (길이 안이면 1).
float Cone(float d, float v) { return saturate(1.0f - d / max(v, 1e-4f)); }
float Cylinder(float d, float v) { return 1.0f - smoothstep(0.95f * v, 1.05f * v, d); }

float4 PS_MotionBlur(float4 position : SV_POSITION) : SV_TARGET
{
    const int2 pixel = int2(position.xy);
    const float3 centerColor = postInput.Load(int3(pixel, 0)).rgb;
    const float2 vc = BlurVector(pixel);
    const float lenC = length(vc);
    // 0.5 픽셀보다 짧으면 번질 것이 없음 → 가운데 색 그대로 (정지 화면은 켜도 끈 것과 비트 단위로 같음)
    if (lenC < 0.5f) return float4(centerColor, 1.0f);

    const float zc = LinearViewDepth(LoadDeviceDepth(pixel));
    const int count = clamp((int)(postBlur.z + 0.5f), 4, 32);
    // 샘플 위치를 픽셀마다 조금씩 밀어 띠(밴딩)를 잡음으로 바꿈. 픽셀 좌표만 쓰므로 프레임마다 같음 (스크린샷 결정적).
    const float jitter = InterleavedGradientNoise(position.xy) - 0.5f;
    const int2 maxPixel = int2(screenParams.xy) - 1;

    // 가운데 샘플의 가중치 1/|vc|: 길게 번지는 픽셀일수록 자기 색의 비중이 줄어듦
    float weight = 1.0f / lenC;
    float3 sum = centerColor * weight;
    for (int i = 0; i < count; ++i)
    {
        const float t = (i + 0.5f + jitter) / count - 0.5f;   // −0.5 ~ 0.5: 픽셀 양쪽으로 반씩
        const float d = abs(t) * lenC;                         // 가운데에서 샘플까지 (픽셀)
        const int2 sp = clamp(int2(floor(position.xy + t * vc)), int2(0, 0), maxPixel);
        const float zs = LinearViewDepth(LoadDeviceDepth(sp));
        const float lenS = length(BlurVector(sp));
        // 앞의 샘플이 자기 속도로 여기까지 번져 옴 + 가운데가 앞이면 가운데의 속도로 뒤를 비춤 + 둘 다 같은 물체처럼 움직임
        const float w = SoftFront(zs, zc) * Cone(d, lenS) + SoftFront(zc, zs) * Cone(d, lenC) + Cylinder(d, lenS) * Cylinder(d, lenC) * 2.0f;
        sum += postInput.Load(int3(sp, 0)).rgb * w;
        weight += w;
    }
    return float4(sum / weight, 1.0f);
}

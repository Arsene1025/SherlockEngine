#include "Temporal.hlsli"

// TAA 리졸브 (3단계, docs/plan/velocity-motionblur-taa-plan.html). 입력 = 시간 셋 (t13 hdr = 지터된 이번 프레임, t23 깊이, t7 속도,
// t24 지난 결과), 출력 = 이번 히스토리. 순서:
//   (1) α ≥ 1 이거나 히스토리가 없으면 (temporalParams.z = 0) 이번 색 그대로 — 배관 검증이 비트 단위로 같도록 맨 앞에서 끝냄
//   (2) 속도 팽창: 3×3 중 가장 가까운 픽셀의 속도 — 물체 가장자리 픽셀이 배경 속도를 따라가 끌리지 않게
//   (3) 지난 위치 = uv − 속도. 화면 밖이면 이번 색
//   (4) 히스토리를 Catmull-Rom 5탭으로 읽음 (쌍선형보다 덜 흐림)
//   (5) 이번 프레임 3×3 이웃의 YCoCg 평균 μ·표준편차 σ 상자 [μ − γσ, μ + γσ] 쪽으로 히스토리를 자름 (가려졌다 드러난 곳의 잔상 제거)
//   (6) 1 / (1 + 밝기) 가중치로 섞음 (Karis 2014) — 아주 밝은 픽셀 하나가 평균을 끌어올려 깜빡이는 것을 막음
//
// b4 PostParams 칸: blur.x = 피드백 α (이번 프레임 비중).

static const float kClipGamma = 1.0f;

float3 RgbToYCoCg(float3 c)
{
    return float3(dot(c, float3(0.25f, 0.5f, 0.25f)), dot(c, float3(0.5f, 0.0f, -0.5f)), dot(c, float3(-0.25f, 0.5f, -0.25f)));
}

float3 YCoCgToRgb(float3 c)
{
    const float t = c.x - c.z;
    return float3(t + c.y, c.x + c.z, t - c.y);
}

float3 LoadCurrent(int2 pixel)
{
    return postInput.Load(int3(clamp(pixel, int2(0, 0), int2(screenParams.xy) - 1), 0)).rgb;
}

// Catmull-Rom 을 쌍선형 샘플 5 번으로 (가운데 2×2 를 한 번에 읽고 네 모서리 탭은 무게가 작아 뺌). 음수 로브 때문에 음수가 나올 수 있어 0 에서 자름.
float3 SampleHistoryCatmullRom(float2 uv)
{
    const float2 samplePos = uv * screenParams.xy;
    const float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;
    const float2 f = samplePos - texPos1;
    const float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    const float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    const float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    const float2 w3 = f * f * (-0.5f + 0.5f * f);
    const float2 w12 = w1 + w2;
    const float2 texPos0 = (texPos1 - 1.0f) * screenParams.zw;
    const float2 texPos3 = (texPos1 + 2.0f) * screenParams.zw;
    const float2 texPos12 = (texPos1 + w2 / w12) * screenParams.zw;

    float3 sum = 0.0f;
    sum += historyTexture.SampleLevel(postSampler, float2(texPos12.x, texPos0.y), 0).rgb * (w12.x * w0.y);
    sum += historyTexture.SampleLevel(postSampler, float2(texPos0.x, texPos12.y), 0).rgb * (w0.x * w12.y);
    sum += historyTexture.SampleLevel(postSampler, float2(texPos12.x, texPos12.y), 0).rgb * (w12.x * w12.y);
    sum += historyTexture.SampleLevel(postSampler, float2(texPos3.x, texPos12.y), 0).rgb * (w3.x * w12.y);
    sum += historyTexture.SampleLevel(postSampler, float2(texPos12.x, texPos3.y), 0).rgb * (w12.x * w3.y);
    const float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(sum / weight, 0.0f);
}

// 히스토리를 상자 가운데 쪽으로 끌어당겨 상자 안에 넣음 (축마다 자르는 것보다 색이 덜 틀어짐 — Playdead INSIDE 의 방식).
float3 ClipTowardCenter(float3 history, float3 center, float3 extent)
{
    const float3 offset = history - center;
    const float3 units = abs(offset / max(extent, 1e-5f));
    const float largest = max(units.x, max(units.y, units.z));
    return largest > 1.0f ? center + offset / largest : history;
}

float4 PS_Taa(float4 position : SV_POSITION) : SV_TARGET
{
    const int2 pixel = int2(position.xy);
    const float3 current = LoadCurrent(pixel);
    const float alpha = postBlur.x;
    if (alpha >= 1.0f || temporalParams.z < 0.5f) return float4(current, 1.0f);   // (1)

    // (2) 속도 팽창 (표준 Z: 작은 깊이가 가까움)
    float closestDepth = 1.0f;
    int2 closestPixel = pixel;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll] for (int dx = -1; dx <= 1; ++dx)
        {
            const int2 p = pixel + int2(dx, dy);
            const float d = LoadDeviceDepth(p);
            if (d < closestDepth) { closestDepth = d; closestPixel = p; }
        }
    }
    const int2 velocityPixel = clamp(closestPixel, int2(0, 0), int2(screenParams.xy) - 1);
    const float2 velocity = velocityTexture.Load(int3(velocityPixel, 0));

    // (3) 지난 프레임에 이 표면이 있던 곳
    const float2 prevUv = position.xy * screenParams.zw - velocity;
    if (any(prevUv < 0.0f) || any(prevUv > 1.0f)) return float4(current, 1.0f);

    // (5) 이웃 통계 (YCoCg: 밝기와 색을 나눠 상자가 덜 길쭉함)
    float3 m1 = 0.0f, m2 = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float3 c = RgbToYCoCg(LoadCurrent(pixel + int2(x, y)));
            m1 += c;
            m2 += c * c;
        }
    }
    const float3 mean = m1 / 9.0f;
    const float3 sigma = sqrt(max(m2 / 9.0f - mean * mean, 0.0f));

    // (4) + (5)
    const float3 history = ClipTowardCenter(RgbToYCoCg(SampleHistoryCatmullRom(prevUv)), mean, sigma * kClipGamma);
    const float3 currentYCoCg = RgbToYCoCg(current);

    // (6) 밝기 역가중 섞기
    const float wCurrent = alpha / (1.0f + max(currentYCoCg.x, 0.0f));
    const float wHistory = (1.0f - alpha) / (1.0f + max(history.x, 0.0f));
    const float3 result = (currentYCoCg * wCurrent + history * wHistory) / (wCurrent + wHistory);
    return float4(max(YCoCgToRgb(result), 0.0f), 1.0f);
}

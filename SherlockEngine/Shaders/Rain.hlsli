// 비 (docs/plan/rain-plan.html). 차폐 맵 (프레임 셋 t24) 과 b0 의 rain* 칸으로 "이 점이 비를 맞는가" 와 젖음을 계산함.
//
// 차폐 맵 = 카메라 위에서 바로 아래로 직교 투영한 깊이 (Renderer::RenderRainOcclusionPass). 한 텍셀이 "이 xz 에서 비가 처음 닿는 높이"
// 를 담음: 월드 y = rainMapY.x − depth × rainMapY.y. 깊이 1 (지운 값) 은 아무것도 없음 = 열린 하늘. 맵 밖도 열린 하늘.
// 비가 없으면 b0 의 rain* 가 모두 0 이라 (rainParams.z = 0) 맵을 읽지 않고 모든 점이 비를 맞는 것으로 보지만, 젖음 (rainParams.y = 0) 과
// 빗줄기는 그리지 않으므로 화면은 그대로.
//
// 빗줄기·튐 셰이더 (RainVertexShader 등) 는 이 파일을 include 하기 전에 RAIN_DRAW 를 정의함 → 비 셋 b8 과 깊이 t23 선언이 추가됨.
// 재질 PS 는 RAIN_DRAW 없이 젖음 함수만 씀 (MaterialSurface.hlsli). 맵은 Load 로만 읽음 (샘플러 없음, 분기 안에서도 안전).
#ifndef SHERLOCK_RAIN_HLSLI
#define SHERLOCK_RAIN_HLSLI

#include "Common.hlsli"

Texture2D<float> rainOcclusionMap : register(t24);

// ---- 해시 (상태 없음. 같은 입력이면 같은 값 — 스크린샷이 결정적) ----
uint RainHash(uint v)
{
    // PCG (Jarzynski & Olano 2020). Particle.hlsli 의 PcgHash 와 같은 식 (그 파일은 b8 을 선언하므로 include 하지 않음).
    const uint state = v * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float RainHash01(uint v)
{
    return (RainHash(v) >> 8) * (1.0f / 16777216.0f);
}

float3 RainHash3(uint index, uint seed)
{
    const uint h = RainHash(index ^ RainHash(seed));
    return float3(RainHash01(h), RainHash01(h ^ 0x68bc21ebu), RainHash01(h ^ 0x02e5be93u));
}

uint RainHash2i(int2 cell, uint salt)
{
    return RainHash(asuint(cell.x) * 73856093u ^ RainHash(asuint(cell.y) * 19349663u ^ salt));
}

// ---- 차폐 맵 ----
bool RainHasMap()
{
    return rainParams.z > 0.5f;
}

// 월드 xz → 맵 텍셀 좌표 (연속값, 텍셀 중심이 .5). 맵 v 는 월드 +z 의 반대 방향 (직교 카메라의 위 = +Z, 텍스처 v 는 아래로).
float2 RainMapPixel(float2 xz)
{
    const float2 uv = float2((xz.x - rainMapXZ.x) * rainMapXZ.z, 1.0f - (xz.y - rainMapXZ.y) * rainMapXZ.z);
    return uv * rainMapY.w;
}

// 텍셀 하나의 차폐 높이. 맵 밖이거나 아무것도 없으면 아주 낮은 값 (= 열린 하늘).
float RainOccluderHeightAt(int2 texel)
{
    const int size = (int)rainMapY.w;
    const bool inside = all(texel >= 0) && all(texel < size);
    const float depth = inside ? rainOcclusionMap.Load(int3(clamp(texel, 0, size - 1), 0)) : 1.0f;
    return depth >= 0.999999f ? -1.0e30f : rainMapY.x - depth * rainMapY.y;
}

// 이 xz 에서 비가 처음 닿는 높이 (점 하나). 맵이 없거나 밖이면 아주 낮은 값.
float RainOccluderHeight(float2 xz)
{
    float height = -1.0e30f;
    if (RainHasMap()) height = RainOccluderHeightAt((int2)floor(RainMapPixel(xz)));
    return height;
}

// 점 p 가 비를 맞는가 (텍셀 하나, 딱 끊김). 빗방울 VS 가 씀.
bool RainExposed(float3 p)
{
    return p.y >= RainOccluderHeight(p.xz) - rainMapY.z;
}

// 표면 점이 비를 맞는 정도 0..1. 표면 법선 쪽으로 텍셀 1.5 개만큼 옮겨서 읽음 — 벽 위의 점은 맵에서 벽 꼭대기 아래라 늘 "가려짐" 이
// 되므로, 벽 바깥 땅의 높이와 비교해야 함 (처마 밑 벽은 처마 높이와 비교되어 마름). 이웃 텍셀 넷의 판정을 쌍선형으로 섞어 지붕 가장자리
// 아래 바닥의 텍셀 계단을 부드럽게 함.
float RainExposure(float3 p, float3 n)
{
    float exposure = 1.0f;
    if (RainHasMap())
    {
        const float3 q = p + n * (1.5f * rainMapXZ.w);
        const float2 pixel = RainMapPixel(q.xz) - 0.5f;
        const int2 base = (int2)floor(pixel);
        const float2 w = pixel - base;
        const float tolerance = rainMapY.z;
        const float e00 = q.y >= RainOccluderHeightAt(base) - tolerance ? 1.0f : 0.0f;
        const float e10 = q.y >= RainOccluderHeightAt(base + int2(1, 0)) - tolerance ? 1.0f : 0.0f;
        const float e01 = q.y >= RainOccluderHeightAt(base + int2(0, 1)) - tolerance ? 1.0f : 0.0f;
        const float e11 = q.y >= RainOccluderHeightAt(base + int2(1, 1)) - tolerance ? 1.0f : 0.0f;
        exposure = lerp(lerp(e00, e10, w.x), lerp(e01, e11, w.x), w.y);
    }
    return exposure;
}

// 젖음 0..1 = 보간된 젖음 × 비를 맞는 정도 × 방향 (위를 보는 면 1, 벽 0.35 — 물이 흘러내림, 아래를 보는 면 0.35).
float RainWetAmount(float3 p, float3 geometricNormal)
{
    float wet = 0.0f;
    if (rainParams.y > 0.0f)
    {
        const float up = saturate(geometricNormal.y);
        wet = rainParams.y * RainExposure(p, geometricNormal) * lerp(0.35f, 1.0f, up);
    }
    return wet;
}

// ---- 웅덩이·물결 (3단계) ----
// 값 노이즈 (격자 꼭짓점 해시 + smoothstep 보간). 0..1.
float RainValueNoise(float2 x)
{
    const float2 i = floor(x);
    float2 f = x - i;
    f = f * f * (3.0f - 2.0f * f);
    const int2 c = (int2)i;
    const float a = RainHash01(RainHash2i(c, 11u));
    const float b = RainHash01(RainHash2i(c + int2(1, 0), 11u));
    const float d = RainHash01(RainHash2i(c + int2(0, 1), 11u));
    const float e = RainHash01(RainHash2i(c + int2(1, 1), 11u));
    return lerp(lerp(a, b, f.x), lerp(d, e, f.x), f.y);
}

// 웅덩이 마스크 0..1. 거의 수평인 면 (기울기 약 18° 이하) 에만, 젖음이 0.5 를 넘은 뒤부터 자람. rainSurface.y (웅덩이 양) 가 클수록 넓음.
float RainPuddle(float3 p, float up, float wet)
{
    const float amount = rainSurface.y;
    float puddle = 0.0f;
    if (amount > 0.0f && wet > 0.5f && up > 0.95f)
    {
        const float n = 0.65f * RainValueNoise(p.xz * 0.35f) + 0.35f * RainValueNoise(p.xz * 1.1f + 17.0f);
        const float threshold = lerp(0.85f, 0.3f, amount);   // 양 0.5 → 문턱 0.575 (평평한 바닥의 약 1/4)
        puddle = smoothstep(threshold, threshold + 0.08f, n) * saturate((wet - 0.5f) / 0.3f) * saturate((up - 0.95f) / 0.04f);
    }
    return puddle;
}

// ---- 빗방울이 닿는 자리 (물결 RainRipple 과 튐 RainSplashVertexShader 가 같이 씀) ----
// 위를 보는 면을 0.35 m 칸으로 나누고 칸 두 겹을 엇갈려 겹침. 칸마다 1.3 Hz 로 빗방울이 하나씩 떨어지고, 떨어질 때마다 (주기 번호를
// 해시에 섞어) 칸 안의 새 자리와 "이번에 떨어지는지" 를 다시 정함. 떨어지는 칸의 비율 = rainSurface.z (빗방울 비율 × 세기, CPU).
// 물결은 그 자리에서 퍼지는 링, 튐은 같은 자리·같은 순간에 터지는 왕관이라 둘이 늘 짝을 이룸. 시간은 둘 다 b0 rainParams.w.
// 칸 크기는 Graphics/RendererRain.cpp 의 kImpactCell 과 같아야 함 (튐 격자 크기 계산).
static const float kRainImpactCell = 0.35f;   // m
static const float kRainImpactRate = 1.3f;    // 칸마다 초당 떨어지는 횟수

struct RainImpact
{
    float2 xz;      // 떨어진 자리 (월드)
    float phase;    // 이번 주기에서 지난 몫 0..1 (떨어진 순간 0). 지난 시간 = phase / kRainImpactRate 초
    bool active;    // 이번 주기에 이 칸에 떨어졌는지
    uint hash;      // 이번 주기의 해시 (튐 크기 등)
};

// 겹 0·1 의 격자 어긋남 (칸 단위). 격자 좌표 = 월드 xz / 칸 + 어긋남.
float2 RainImpactLayerOffset(int layer)
{
    return layer == 0 ? float2(0.0f, 0.0f) : float2(0.5f, 0.37f);
}

RainImpact RainImpactInCell(int2 cell, int layer)
{
    const uint h = RainHash2i(cell, 101u + (uint)layer);
    const float cyclePosition = rainParams.w * kRainImpactRate + RainHash01(h ^ 0x27d4eb2du);   // 칸마다 시작 시각이 다름
    const float cycle = floor(cyclePosition);
    const uint hc = RainHash(h ^ RainHash((uint)(int)cycle));
    const float2 center = float2(RainHash01(hc), RainHash01(hc ^ 0x5bd1e995u)) * 0.5f + 0.25f;   // 칸 가운데 절반 — 링이 칸을 넘지 않음

    RainImpact impact;
    impact.xz = ((float2)cell + center - RainImpactLayerOffset(layer)) * kRainImpactCell;
    impact.phase = cyclePosition - cycle;
    impact.active = RainHash01(hc ^ 0x165667b1u) < rainSurface.z;
    impact.hash = hc;
    return impact;
}

// 빗방울이 떨어진 자리에서 퍼지는 동심원 (Lagarde, "Water drop 2b"). 돌려주는 값 = 노멀의 xz 기울기 (높이장의 기울기).
// 링이 생기는 칸의 비율 = rainSurface.z — 빗방울이 적을수록 드문드문. 비가 그치면 (z = 0) 0.
float2 RainRipple(float3 p)
{
    const float strength = rainSurface.x;
    const float density = rainSurface.z;
    const float distanceFade = saturate(1.0f - length(cameraPosition - p) / 25.0f);   // 멀리서는 픽셀보다 작아 반짝이기만 함
    float2 slope = float2(0.0f, 0.0f);
    if (strength > 0.0f && density > 0.0f && distanceFade > 0.0f)
    {
        [unroll]
        for (int layer = 0; layer < 2; ++layer)
        {
            const int2 cell = (int2)floor(p.xz / kRainImpactCell + RainImpactLayerOffset(layer));
            const RainImpact impact = RainImpactInCell(cell, layer);
            const float2 offset = (p.xz - impact.xz) / kRainImpactCell;   // 칸 단위
            const float d = length(offset);
            const float radius = impact.phase * 0.25f;   // 칸의 1/4 까지 퍼짐
            const float x = (d - radius) * 24.0f;        // 링 단면 (−π..π 가 링 하나)
            if (impact.active && abs(x) < 3.14159265f && d > 1.0e-4f)
            {
                const float fade = (1.0f - impact.phase) * (1.0f - impact.phase);
                slope += (offset / d) * cos(x * 0.5f) * sin(x) * fade;
            }
        }
        slope *= 0.35f * strength * distanceFade;
    }
    return slope;
}

#ifdef RAIN_DRAW
// b8 비 셋 (Layout_Rain). ShaderConstants.h 의 RainConstants 와 같은 순서 (96 바이트).
cbuffer Rain : register(b8)
{
    float3 rainFallOffset;    // 축마다 fmod(velocity × t, boxSize)
    float rainBoxBelow;       // 상자에서 카메라 아래 몫
    float3 rainBoxSize;       // 상자 크기 (m)
    uint rainDropCount;
    float3 rainVelocity;      // m/s
    float rainStreakLength;   // m
    float3 rainColor;         // color × brightness
    float rainStreakWidth;    // m
    float rainNearFade;       // m
    float rainSoftDistance;   // m
    uint rainSeed;
    uint rainSplashCount;
    uint rainSplashGrid;      // 튐 격자 한 변의 칸 수 (겹 하나 = 한 변²). 튐 번호 → 칸
    float rainSplashRadius;   // m
    float rainSplashPeriod;   // 초
    float rainSplashSize;     // m
}

// t23: 이 뷰의 깊이 (파티클 깊이 셋, DepthRead). 빗줄기·튐이 장면과 만나는 곳을 흐리게 함.
Texture2D<float> rainSceneDepth : register(t23);

// 거리 viewZ 에서 화면 픽셀 하나의 월드 크기 (세로 기준).
float RainPixelWorldSize(float viewZ)
{
    const bool perspective = proj[2][3] > 0.5f;
    return (perspective ? max(viewZ, 0.01f) : 1.0f) * 2.0f / (proj[1][1] * screenParams.y);
}

// 환경광 세기 (어두운 실내 씬에서 빗줄기가 혼자 밝게 뜨지 않게). 환경 맵이 없으면 1.
float RainEnvironmentScale()
{
    return environmentParams.w > 0.5f ? environmentParams.x : 1.0f;
}

// 빗줄기·튐 VS → PS.
struct RainVSOutput
{
    float4 position : SV_POSITION;
    float2 quad : TEXCOORD0;    // 쿼드 안 위치. 빗줄기: x = 가로 −1..1, y = 머리 0 → 꼬리 1. 튐: −1..1
    float3 color : TEXCOORD1;   // 페이드까지 곱한 HDR 색
    float2 extra : TEXCOORD2;   // x = 뷰 z (소프트 페이드), y = 튐의 진행도 0..1 (빗줄기는 0)
};

// 장면 깊이와 가까우면 흐리게 (소프트 파티클과 같은 식). distance = 흐려지는 거리 (m). 빗줄기는 rainSoftDistance, 튐은 면 위에
// 붙어 있으므로 그보다 훨씬 짧게.
float RainSoftFade(float4 svPosition, float viewZ, float distance)
{
    const float sceneZ = ViewDepthFromDevice(rainSceneDepth.Load(int3(svPosition.xy, 0)));
    return distance > 0.0f ? saturate((sceneZ - viewZ) / distance) : 1.0f;
}
#endif // RAIN_DRAW

#endif // SHERLOCK_RAIN_HLSLI

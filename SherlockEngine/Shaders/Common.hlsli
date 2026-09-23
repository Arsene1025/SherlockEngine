// SherlockEngine 공용 셰이더 헤더.
// 상수버퍼 레이아웃은 Graphics/ShaderConstants.h 와 바이트 단위로 같아야 한다.
// C++ 쪽 static_assert와 Debug 빌드의 D3DReflect 대조가 이를 검사한다.
//
// register(b#, space0) 표기는 SM 5.0 fxc가 error X3721로 거부한다 (5.1 이상만 지원). 원문의
// "fxc가 무시한다"는 틀렸다. space는 8단계에서 SM 5.1/6.0으로 올릴 때 넣는다.
//
// 레지스터 배정 (6단계, ShaderConstants.h 와 동일):
//   재질 셋:   t0..t7  s0..s3   — 알베도 t0, 노멀 t1, 재질 샘플러 s0
//   프레임 셋: t8..t15 s4..s7   — 그림자 맵 t8, 비교 샘플러 s4
#ifndef SHERLOCK_COMMON_HLSLI
#define SHERLOCK_COMMON_HLSLI

static const uint MAX_LIGHTS = 8;
static const uint LIGHT_DIRECTIONAL = 0;
static const uint LIGHT_POINT = 1;
static const uint LIGHT_SPOT = 2;
static const float EPSILON = 0.0001f;

// 80바이트 = 16바이트 레지스터 5개. float3 뒤에 스칼라를 두어 경계를 맞춘다.
struct LightData
{
    float3 position;
    float intensity;

    float3 direction;
    float range;

    float3 color;
    uint type;

    float3 attenuation;
    float innerConeCos;

    float outerConeCos;
    float3 padding;
};

// b0: 프레임에 한 번. VS와 PS 모두 읽는다. (304바이트)
cbuffer PerFrame : register(b0)
{
    matrix view;
    matrix proj;
    matrix viewProj;
    matrix lightViewProj;     // 6단계: 방향광 0의 그림자 행렬 (월드 → 광원 클립)
    float3 cameraPosition;
    float time;
    float4 shadowParams;      // x = 1/맵 크기, y = 깊이 바이어스, z = 세기(0..1), w = 사용 여부
    float4 debugParams;       // 11단계: x = 디버그 뷰 (0 조명, 1 알베도, 2 노멀, 3 깊이, 4 그림자, 5 UV), y = 깊이 범위
}

// b1: 드로우마다. VS만. (128바이트)
cbuffer PerObject : register(b1)
{
    matrix world;
    matrix worldInvTranspose;
}

// b2: 조명. PS만. (656바이트)
cbuffer Lights : register(b2)
{
    LightData lights[MAX_LIGHTS];
    float3 ambientColor;
    uint lightCount;
}

// b3: 재질. VS(uvScale)와 PS 모두. (64바이트)
cbuffer Material : register(b3)
{
    float4 baseColor;
    float3 specularColor;
    float shininess;
    float2 uvScale;
    float unlit;
    float normalStrength;     // 6단계: 노멀 맵 xy 배율. 0 = 무시
    float alphaCutoff;        // 9단계: 알파 컷아웃. 0 = 끔
    float3 materialPadding;
}

// 재질 셋 (Layout_Material)
Texture2D albedoTexture : register(t0);
Texture2D normalTexture : register(t1);
SamplerState albedoSampler : register(s0);

// 프레임 셋 (Layout_Frame). 그림자 맵은 R32_FLOAT 로 읽는 깊이 텍스처.
Texture2D shadowMap : register(t8);
SamplerComparisonState shadowSampler : register(s4);

struct VSInput
{
    float3 position : POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;   // xyz = u 증가 방향, w = 손잡이. B = cross(N, T) * w
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float4 shadowPosition : TEXCOORD2;   // 광원 클립 공간
    float4 tangent : TANGENT0;
};

float3 SafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return (lengthSquared > EPSILON * EPSILON) ? value * rsqrt(lengthSquared) : fallback;
}

#endif // SHERLOCK_COMMON_HLSLI

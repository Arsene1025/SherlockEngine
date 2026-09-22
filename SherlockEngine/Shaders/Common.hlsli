// SherlockEngine 공용 셰이더 헤더.
// 상수버퍼 레이아웃은 Graphics/ShaderConstants.h 와 바이트 단위로 같아야 한다.
// C++ 쪽 static_assert와 Debug 빌드의 D3DReflect 대조가 이를 검사한다.
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

// b0: 프레임에 한 번. VS와 PS 모두 읽는다. (208바이트)
cbuffer PerFrame : register(b0)
{
    matrix view;
    matrix proj;
    matrix viewProj;
    float3 cameraPosition;
    float time;
}

// b1: 드로우마다. VS만. (128바이트)
cbuffer PerObject : register(b1)
{
    matrix world;
    matrix worldInvTranspose;
}

// b2: 조명. PS만. (672바이트)
cbuffer Lights : register(b2)
{
    LightData lights[MAX_LIGHTS];
    float3 ambientColor;
    uint lightCount;
    float3 specularColor;
    float shininess;
}

struct VSInput
{
    float3 position : POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
    float3 worldPosition : TEXCOORD0;
};

float3 SafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return (lengthSquared > EPSILON * EPSILON) ? value * rsqrt(lengthSquared) : fallback;
}

#endif // SHERLOCK_COMMON_HLSLI

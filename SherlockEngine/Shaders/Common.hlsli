// SherlockEngine 공용 셰이더 헤더: 모든 셰이더가 쓰는 상수·b0 PerFrame·수학 함수만 둠.
// 나머지는 C++ 바인딩 셋 (Graphics/Renderer.cpp 의 CreateLayouts, Layout_*) 단위로 나눈 헤더에 있음 — 셋 하나의 선언은
// 그 셋의 헤더에만 두고, 셰이더 하나만 쓰는 슬롯은 그 셰이더 파일에 둠:
//   Lighting.hlsli      Layout_Frame 의 나머지 (조명·그림자·환경·IBL)
//   Mesh.hlsli          Layout_Object (b1, b7) 와 메시 정점 입출력
//   Material.hlsli      Layout_Material
//   Post.hlsli          Layout_Post · Layout_BlurCompute · Layout_BlurWeights 공용 (b4, t20)
//   Sky.hlsli           스카이박스 VS → PS
//   Particle.hlsli      파티클 공용 (b8, t23, Particle) · ParticleSim.hlsli 시뮬레이션 셋 · ParticleSort.hlsli 정렬 셋
// 셰이더가 include 하는 헤더는 (재귀로) .cso 신선도 검사와 핫리로드 감시 대상 (ShaderCompiler::ListIncludeFiles).
//
// 상수버퍼 레이아웃은 Graphics/ShaderConstants.h 와 바이트 단위로 같아야 함.
// C++ 쪽 static_assert와 Debug 빌드의 D3DReflect 대조로 이를 검사함.
//
// register(b#, space0) 표기는 SM 5.0 fxc가 error X3721로 거부함 (5.1 이상만 지원).
// space 는 SM 5.1/6.0 으로 올릴 때 넣음.
#ifndef SHERLOCK_COMMON_HLSLI
#define SHERLOCK_COMMON_HLSLI

static const float EPSILON = 0.0001f;
static const float PI = 3.14159265f;

// b0: 프레임에 한 번. VS와 PS 모두 읽음. (304바이트)
cbuffer PerFrame : register(b0)
{
    matrix view;
    matrix proj;
    matrix viewProj;
    float3 cameraPosition;
    float time;
    float4 shadowParams;      // x = 1/맵 크기, y = 깊이 바이어스, z = 세기(0..1), w = 사용 여부
    float4 debugParams;       // x = 디버그 뷰 (0 조명, 1 알베도, 2 노멀, 3 깊이, 4 그림자, 5 UV, 6 금속성/거칠기, 7 AO), y = 깊이 범위
    float4 environmentParams; // x = IBL 세기, y = 반사 큐브 밉 수, z = 확산 조도를 읽을 밉, w = 환경 있음
    float4 outputParams;      // x = 노출, y = 톤매핑, z = BRDF LUT 있음
    float4 shadowExtra;       // x = 노말 섀도 오프셋 (노말맵이 그림자 UV 를 미는 배율)
    float4 iblTint;           // xyz = IBL 색 배율 (확산 조도·반사 프리필터에 곱함)
}

float3 SafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return (lengthSquared > EPSILON * EPSILON) ? value * rsqrt(lengthSquared) : fallback;
}

#endif // SHERLOCK_COMMON_HLSLI

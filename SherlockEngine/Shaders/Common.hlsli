// SherlockEngine 공용 셰이더 헤더.
// 상수버퍼 레이아웃은 Graphics/ShaderConstants.h 와 바이트 단위로 같아야 함.
// C++ 쪽 static_assert와 Debug 빌드의 D3DReflect 대조로 이를 검사함.
//
// register(b#, space0) 표기는 SM 5.0 fxc가 error X3721로 거부함 (5.1 이상만 지원).
// space 는 SM 5.1/6.0 으로 올릴 때 넣음.
//
// 레지스터 배정 (ShaderConstants.h 와 동일):
//   재질 셋:   t0..t7  s0..s3   — 알베도 t0, 노멀 t1, 금속성/거칠기 t2, AO t3, 발광 t4, 반사 세기 t5, 반사 색 t6, 클리어코트 t16~t18, 투과 t19, 재질 샘플러 s0
//   프레임 셋: t8..t15 s4..s7   — 그림자 맵 t8, 비교 샘플러 s4, 스카이박스 t9, 환경 샘플러 s5,
//                                IBL 확산 조도 t10, IBL 반사 프리필터 t11, BRDF LUT t12, CSM 아틀라스 t14,
//                                조명 구조체 버퍼 t15
#ifndef SHERLOCK_COMMON_HLSLI
#define SHERLOCK_COMMON_HLSLI

static const uint MAX_SHADOW_VIEWS = 20;   // Scene/Light.h 의 MAX_SHADOW_VIEWS 와 같아야 함 (b6 크기 2000B 가 이 값에 달림)
static const uint LIGHT_DIRECTIONAL = 0;
static const uint LIGHT_POINT = 1;
static const uint LIGHT_SPOT = 2;
static const float EPSILON = 0.0001f;
static const float PI = 3.14159265f;

// 80바이트 = 16바이트 레지스터 5개. float3 뒤에 스칼라를 두어 경계를 맞춤.
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

    // shadow옵션
    uint castShadows;
    int shadowIndex;
    float padding;
};

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

// b4: 후처리 패스 파라미터. 후처리 셋(톤 패스·블러·블룸)만 바인딩함. (80바이트, ShaderConstants.h 의
// PostParamsConstants)
cbuffer PostParams : register(b4)
{
    float4 postPass;          // x = 모드 (1 블러, 2 블룸 밝기 추출+축소, 3 축소, 4 확대), y,z = 블러 방향 (텍셀 단위)
    float4 postBlur;          // x = 시그마(픽셀), y = 반경(픽셀)
    float4 postBloom;         // x = 임계값, y = knee, z = 확대 결과 배율
    float4 postView;          // xy = 입력 크기, zw = 출력 크기 (픽셀)
    float4 postGrade;         // x = GreyScale 양, y = 1 이면 색 효과를 건너뜀
}

// 블러 가우시안 가중치 w[i] = exp(-i^2 / 2σ^2), i = 0..반경 (반쪽만, 대칭). BlurWeightsComputeShader 가 블러마다 쓰고
// PS 블러(PostPixelShader 모드 1)와 CS 블러(BlurComputeShader)가 같은 버퍼를 읽음 — 두 경로가 가중치를 루프 안에서
// 따로 계산하던 때는 D3D11 에서 결과가 최하위 비트만큼 달랐음 (어느 연산 때문인지는 특정하지 못함). 후처리 셋·컴퓨트 블러 셋의 t20.
StructuredBuffer<float> blurWeights : register(t20);
static const int kMaxBlurRadius = 64;   // ShaderConstants.h 의 kMaxBlurRadius 와 같아야 함 (가중치 버퍼 = 65 개)

// b1: 드로우마다. VS만. (128바이트)
cbuffer PerObject : register(b1)
{
    matrix world;
    matrix worldInvTranspose;
}

// b7: 스키닝 조인트 행렬 팔레트. 스킨 VS(BasicSkinVertexShader, ShadowSkinVertexShader)만 읽음. (16384바이트)
// palette[j] = 역바인드 · 조인트 월드 · (메시 노드 월드)⁻¹ 를 C++ 가 전치해서 올림 (world 와 같은 관례, mul(v, M)).
static const uint MAX_JOINTS = 256;   // ShaderConstants.h 의 kMaxSkinJoints 와 같아야 함
cbuffer Skin : register(b7)
{
    matrix joints[MAX_JOINTS];
}

// 정점의 조인트 번호 4개·가중치 4개로 섞은 스킨 행렬. 가중치 합이 1 이 되게 임포터가 맞춰 둠.
matrix SkinMatrix(uint4 jointIndices, float4 jointWeights)
{
    return joints[jointIndices.x] * jointWeights.x
         + joints[jointIndices.y] * jointWeights.y
         + joints[jointIndices.z] * jointWeights.z
         + joints[jointIndices.w] * jointWeights.w;
}

// b2: 조명 공통 값. PS만. (16바이트)
cbuffer Lights : register(b2)
{
    float3 ambientColor;
    uint lightCount;          // 아래 lights 의 유효 원소 수
}

// t15: 조명 배열 (프레임 셋). cbuffer 의 고정 크기 배열이 아니라 구조체 버퍼라 조명 수에 상한이 없음.
// 원소 배치는 Graphics/ShaderConstants.h 의 GpuLight 와 같아야 함 (80바이트).
StructuredBuffer<LightData> lights : register(t15);

struct ShadowViewData
{
    matrix viewProj;
    float4 atlasRect;
    float4 params;
};

// b5: 그림자 패스. VS 만. 뷰마다 다시 올림 (64바이트)
cbuffer ShadowPass : register(b5)
{
    matrix shadowViewProj;
}

// b6: 그림자 뷰 배열. PS 만 (2000바이트)
cbuffer Shadows : register(b6)
{
    ShadowViewData shadowViews[MAX_SHADOW_VIEWS];
    float4 cascadeSplits;
    float4 cascadeOrigin;
    float4 cascadeForward;
    float4 shadowAtlasParams;
    float4 csmAtlasParams;    // xy = 1/CSM 아틀라스 (가로, 세로)
}

// b3: 재질. VS(uvScale)와 PS 모두. (112바이트)
cbuffer Material : register(b3)
{
    float4 baseColor;
    float3 emissive;          // PBR: 발광 계수 (선형)
    float metallic;           // 0 = 유전체, 1 = 금속
    float2 uvScale;
    float unlit;
    float normalStrength;     // 노멀 맵 xy 배율. 0 = 무시
    float alphaCutoff;        // 알파 컷아웃. 0 = 끔
    float roughness;          // 지각적 거칠기. α = roughness²
    float occlusionStrength;  // AO 텍스처를 얼마나 따를지
    float ior;                // KHR_materials_ior. 1.5 = F0 0.04
    float3 specularColor;     // KHR_materials_specular: 비금속 F0 에 곱함
    float specularFactor;     // 비금속 반사 세기. F90 으로도 쓰임
    float clearcoatFactor;    // KHR_materials_clearcoat. 0 = 끔
    float clearcoatRoughness;
    float clearcoatNormalScale;
    float transmissionFactor; // KHR_materials_transmission. 0 = 끔
    float blended;            // 1 = 블렌딩 패스 재질. 출력 rgb 에 알파를 미리 곱함 (블렌드: One, InvSrcAlpha)
    float materialPadding1;
    float materialPadding2;
    float materialPadding3;
}

// 재질 셋 (Layout_Material)
Texture2D albedoTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D metallicRoughnessTexture : register(t2);   // G = 거칠기, B = 금속성 (glTF)
Texture2D occlusionTexture : register(t3);           // R = AO
Texture2D emissiveTexture : register(t4);            // sRGB
Texture2D specularTexture : register(t5);            // A = specularFactor 에 곱함 (KHR_materials_specular)
Texture2D specularColorTexture : register(t6);       // sRGB. specularColor 에 곱함
Texture2D clearcoatTexture : register(t16);          // R = clearcoatFactor 에 곱함 (KHR_materials_clearcoat). t7~t15 는 프레임 셋·예약이라 t16 부터
Texture2D clearcoatRoughnessTexture : register(t17); // G = clearcoatRoughness 에 곱함
Texture2D transmissionTexture : register(t19);       // R = transmissionFactor 에 곱함 (KHR_materials_transmission)
Texture2D clearcoatNormalTexture : register(t18);    // 코팅 노멀 맵 (선형). 비면 평평한 노멀 = 기하 노멀
SamplerState albedoSampler : register(s0);

// 프레임 셋 (Layout_Frame). 그림자 맵은 R32_FLOAT 로 읽는 깊이 텍스처.
Texture2D shadowMap : register(t8);
SamplerComparisonState shadowSampler : register(s4);
// 방향광 CSM 전용 아틀라스 (캐스케이드마다 해상도가 다름). ShadowViewData.params.w == 1 인 뷰가 읽음. 같은
// 비교 샘플러(s4)를 씀.
Texture2D csmMap : register(t14);
// 씬의 스카이박스 큐브맵 (없으면 1×1 검은 큐브). 방향 벡터로 샘플함 — 길이는 상관없음.
// 프레임 셋에 둔 이유: 스카이박스뿐 아니라 이후 반사·환경광(IBL)도 모든 재질이 같은 큐브를 읽기 때문.
TextureCube environmentMap : register(t9);
SamplerState environmentSampler : register(s5);
// IBL. 셋 다 같은 선형 Clamp 샘플러(s5)로 읽음.
//   t10 확산 조도: 표면 노멀 방향의 코사인 가중 평균 복사휘도. albedo 를 곱하면 곧 확산 반사광 (π 는 이미 들어 있음)
//   t11 반사 프리필터: 반사 방향으로 읽음. 밉 0 = 거칠기 0(거울), 마지막 밉 = 거칠기 1
//   t12 BRDF LUT: u = N·V, v = 1 − roughness. RG = F0 에 곱할 값과 더할 값 (Karis 의 split-sum)
// 씬에 IBL 맵이 없고 스카이박스만 있으면 Renderer 가 스카이박스를 t10·t11 에 대신 꽂음(밉으로 근사).
TextureCube irradianceMap : register(t10);
TextureCube prefilteredMap : register(t11);
Texture2D brdfLut : register(t12);

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
    float4 tangent : TANGENT0;
};

// ---- GPU 파티클 (VFX) ----
// b8: 이미터 하나. ShaderConstants.h 의 ParticleEmitterConstants 와 같은 순서 (112바이트). 시뮬레이션 셋(CS)과
// 그리기 셋(VS/PS)이 같은 버퍼를 꽂음.
cbuffer ParticleEmitter : register(b8)
{
    float3 particleEmitterPosition;
    float particleDeltaTime;
    float3 particleDirection;
    float particleSpreadCos;
    float3 particleColor;          // 선형 RGB × intensity (HDR)
    float particleLifetime;
    float3 particleGravity;
    float particleEmitRadius;
    float particleStartSpeed;
    float particleSpeedVariation;
    float particleStartSize;
    float particleEndSize;
    uint particleCapacity;         // 버퍼 원소 수
    uint particleSpawnCount;       // 이번 프레임에 태어나길 원하는 수 (사망 목록이 모자라면 그만큼만 태어남)
    uint particleSeed;
    uint particleCollide;          // 1 이면 깊이 버퍼 충돌 (CS_ParticleSim)
    float particleSoftDistance;    // 소프트 파티클 거리 (m). 0 = 끔
    float particleBounce;          // 충돌 반발 계수
    float particleOpacity;         // 알파 모드 불투명도
    uint particleBlendMode;        // 0 가산, 1 알파 (프리멀티플라이드)
    float3 particleEndTint;        // 수명 끝의 색 배율 (1 → endTint)
    float particlePadding;
}

// t23: 씬 깊이 (D24S8 의 깊이, DepthRead 상태 — 읽기 전용 DSV 와 동시에 읽음). 파티클 PS (소프트) · CS (충돌).
Texture2D<float> particleSceneDepth : register(t23);

// 깊이 버퍼 값 → 뷰 공간 z. a = 투영 [2][2], b = [3][2] (행 벡터 기준). 원근: d = a + b / z, 직교: d = a z + b.
float LinearViewDepth(float d, float a, float b, float perspective)
{
    return perspective > 0.5f ? b / (d - a) : (d - b) / a;
}

// 파티클 하나 (32바이트, ShaderConstants.h 의 kParticleStride). age >= lifetime 이면 죽은 파티클 — 그리지 않음.
// 버퍼를 새로 만든 프레임에 CS_ParticleReset 이 전부 (age 1, lifetime 0) 으로 지움.
struct Particle
{
    float3 position;   // 월드
    float age;         // 초
    float3 velocity;   // m/s, 월드
    float lifetime;    // 초. 태어날 때 이미터 값을 복사함
};

#ifdef PARTICLE_SORT
// 알파 이미터의 뷰별 정렬 (바이토닉, CS_ParticleSortInit / CS_ParticleSortStep). 셋 0 = 이미터·뷰마다, 셋 1 = 단계마다.
//   t21 파티클, t22 이번 프레임 생존 목록, t24 개수 (kCounterSimCount = 이번 프레임 최종 생존 수)
//   u0 키 (뷰 공간 z), u1 파티클 번호. 길이 = capacity 를 2 의 거듭제곱으로 올린 것 (ParticleSortCount)
//   b10 이 뷰의 view 행렬, b11 이 단계의 (k, j)
StructuredBuffer<Particle> sortParticles : register(t21);
StructuredBuffer<uint> sortAliveList : register(t22);
StructuredBuffer<uint> sortCounters : register(t24);
RWStructuredBuffer<float> sortKeys : register(u0);
RWStructuredBuffer<uint> sortIndices : register(u1);
cbuffer ParticleSortView : register(b10)
{
    matrix sortView;
}
cbuffer ParticleSortStep : register(b11)
{
    uint sortK;            // 비교하는 바이토닉 열의 길이 (2, 4, … N)
    uint sortJ;            // 비교 거리 (k/2, … 1)
    uint2 sortStepPadding;
    float4 sortStepPadding2[15];   // 256 바이트 (D3D12 가 CB 를 256 으로 올림 — 초기 데이터 크기와 맞춤)
}
static const uint kSortCounterSimCount = 2;   // PARTICLE_SIM 의 kCounterSimCount 와 같음

// 정렬 길이: capacity 이상인 가장 작은 2 의 거듭제곱 (최소 64 = 그룹 하나). ShaderConstants.h 의 ParticleSortCount 와 같은 식.
uint ParticleSortCount(uint capacity)
{
    return capacity <= 64 ? 64 : (1u << (firstbithigh(capacity - 1) + 1));
}
#endif

#ifdef PARTICLE_SIM
// 파티클 시뮬레이션 셋 (Layout_ParticleSim, 이미터마다). 컴퓨트 셰이더 4개가 이 선언을 같이 씀 — 핫리로드가 감시하는
// Common.hlsli 에 두려고 PARTICLE_SIM 을 정의한 파일에만 보이게 함.
//
//   u0 particlesRW      파티클 capacity 개
//   u1 particleDead     사망 목록: 비어 있는 슬롯 번호. [0, 카운터 Dead) 가 유효
//   u2 particleAliveIn  지난 프레임 생존 목록 (시뮬레이션이 읽음)
//   u3 particleAliveOut 이번 프레임 생존 목록 (시뮬레이션·생성이 씀, VS 가 t22 로 읽음). 프레임마다 u2 와 바뀜
//   u4 particleCounters 개수들 (아래 kCounter*)
//   u5 particleArgs     간접 인자 (raw). 바이트 0: 그리기 {6, 생존 수, 0, 0}, 바이트 16: 시뮬레이션 디스패치 {그룹 수, 1, 1}
RWStructuredBuffer<Particle> particlesRW : register(u0);
RWStructuredBuffer<uint> particleDead : register(u1);
RWStructuredBuffer<uint> particleAliveIn : register(u2);
RWStructuredBuffer<uint> particleAliveOut : register(u3);
RWStructuredBuffer<uint> particleCounters : register(u4);
RWByteAddressBuffer particleArgs : register(u5);

static const uint kCounterAlive = 0;       // 이번 프레임 생존 목록에 지금까지 들어간 수 (시뮬레이션이 원자적으로 늘림)
static const uint kCounterDead = 1;        // 사망 목록 길이
static const uint kCounterSimCount = 2;    // 이번 프레임 시뮬레이션이 훑을 수 (= 지난 프레임 최종 생존 수)
static const uint kCounterEmitCount = 3;   // 이번 프레임 실제로 태어나는 수
static const uint kCounterEmitDeadBase = 4;  // 생성이 꺼내 쓸 사망 목록 구간의 시작
static const uint kCounterEmitAliveBase = 5; // 생성이 쓸 생존 목록 구간의 시작
static const uint kParticleCounterCount = 8; // ShaderConstants.h 의 kParticleCounterCount
static const uint kArgsDrawOffset = 0;
static const uint kArgsDispatchOffset = 16;
static const uint kParticleSimGroupSize = 64;

// b9: 파티클 충돌 (ShaderConstants.h 의 ParticleCollisionConstants). 메인 뷰 카메라, 프레임에 한 번.
cbuffer ParticleCollision : register(b9)
{
    matrix collisionViewProj;
    matrix collisionInvViewProj;
    float collisionDepthA;
    float collisionDepthB;
    float collisionPerspective;
    float collisionThickness;
    uint2 collisionDepthSize;
    uint2 collisionPadding;
}
#endif

// 파티클 VS → PS (ParticleVertexShader / ParticlePixelShader).
struct ParticleVSOutput
{
    float4 position : SV_POSITION;
    float2 corner : TEXCOORD0;    // 쿼드 안 위치 (−1..1). PS 가 원형 감쇠에 씀
    float3 color : TEXCOORD1;     // 수명에 따른 페이드까지 곱한 HDR 색
    float2 depthFade : TEXCOORD2; // x = 뷰 공간 z (소프트 파티클), y = 페이드 (알파 모드의 알파)
};

// PCG 해시 (Jarzynski & Olano 2020). 정수 하나 → 고르게 섞인 정수. 상태를 들고 다니지 않아 스레드마다
// (씨앗, 인덱스) 로 독립된 난수열을 만들 수 있음 — 같은 입력이면 같은 결과 (스크린샷 비교가 결정적).
uint PcgHash(uint v)
{
    const uint state = v * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// [0, 1) 난수. 상태를 한 번 섞고 위 24비트를 씀 (float 가수부에 정확히 들어감).
float Random01(inout uint state)
{
    state = PcgHash(state);
    return (state >> 8) * (1.0f / 16777216.0f);
}

// 스카이박스 VS → PS (SkyboxVertexShader / SkyboxPixelShader).
struct SkyVSOutput
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD0;   // 월드 공간 방향 (정규화 전). 큐브 샘플은 방향만 보므로 길이는 상관없음
};

float3 SafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return (lengthSquared > EPSILON * EPSILON) ? value * rsqrt(lengthSquared) : fallback;
}

// 월드 행렬이 반사(행렬식 < 0, 예: scale -1 미러)이면 노멀((W⁻¹)ᵀ)과 탄젠트(W)가 따로 변환되어 cross(N, T) 의
// 방향이 뒤집힘. 그래서 탄젠트 손잡이 w 에 행렬식의 부호를 곱해야 B = cross(N, T) * w 가 표면을 제대로
// 따라감. 스케일 0 이어도 0 이 되지 않도록 sign() 대신 삼항을 씀. 행렬식 부호는 전치해도 같아서 C++ 가 전치해
// 올린 world 를 그대로 써도 됨. 탄젠트를 출력하는 모든 VS(디퍼드 G-buffer 포함)가 이 함수를 거칠 것.
float TangentHandedness(float tangentW, matrix worldMatrix)
{
    const float det = dot(cross(worldMatrix[0].xyz, worldMatrix[1].xyz), worldMatrix[2].xyz);
    return det < 0.0f ? -tangentW : tangentW;
}

// 최종 출력. 노출을 곱하고, 켜져 있으면 ACES 근사(Narkowicz 2016)로 [0, ∞) → [0, 1] 로 누름.
// 꺼져 있으면 1 을 넘는 값은 출력 타깃에서 잘림. 결과는 선형 — sRGB RTV 가 인코딩함.
// HDR: 이 함수는 TonemapPixelShader 만 부름 (메인·스카이 PS 는 선형 HDR 값을 FP16 타깃에 그대로 씀).
// 그래서 "sRGB RTV" 는 톤 패스의 출력 뷰임.
float3 ApplyExposureToneMap(float3 color)
{
    color *= outputParams.x;
    if (outputParams.y > 0.5f)
    {
        color = saturate((color * (2.51f * color + 0.03f)) / (color * (2.43f * color + 0.59f) + 0.14f));
    }
    return color;
}

#endif // SHERLOCK_COMMON_HLSLI

#pragma once
#include <cstdint>
#include <cstddef>
#include <DirectXMath.h>
#include "Scene/Light.h"

// 상수버퍼 레이아웃. Shaders/*.hlsli 의 cbuffer 와 바이트 단위로 같아야 함 (헤더는 바인딩 셋 단위로 나뉨 — Common.hlsli 머리 주석의 목록).
//
//   갱신 빈도별로 나눔
//   b0 PerFrame  — 프레임에 한 번. 뷰·투영·광원 ViewProj·카메라
//   위치·시간·그림자·환경광(IBL)·출력(노출·톤매핑) 파라미터. VS와 PS 모두.
//   b1 PerObject — 드로우마다. 월드 행렬과 노멀 변환용 역전치.          VS만.
//   b2 Lights    — 프레임에 한 번(조명이 바뀔 때만이어도 됨).           PS만.
//   b3 Material  — 재질마다. 기본색·금속성·거칠기·발광·UV 배율·노멀 맵 세기·AO 세기. VS(uvScale)와 PS.
// 예전에는 b0 하나(world·view·proj·wvp, 320B)를 드로우마다 통째로 올렸음.
//
// 패킹 규칙 (HLSL "Packing Rules for Constant Variables"):
//   (1) 16바이트 레지스터 단위. 벡터는 레지스터 경계를 넘지 못함.
//       → C++의 XMFLOAT3 뒤에는 반드시 4바이트 스칼라(또는 명시적 pad)를 둬야 함.
//   (2) matrix(float4x4) = 64B. 구조체 배열의 원소는 16B 정렬.
//   (3) HLSL uint ↔ C++ uint32_t.
//   (4) alignas(16)과 "sizeof % 16 == 0"은 필요조건일 뿐임. float3 두 개를 나란히
//       두면 크기는 맞아도 오프셋이 어긋남. 그래서 아래 offsetof 단언이 실제 검사임.
//
//   행렬 관례:
//   HLSL은 기본적으로 column_major 로 읽고, 셰이더는 mul(v, M) 행벡터 곱을 씀.
//   DirectXMath 행렬(행우선 메모리)을 그대로 올리면 HLSL이 전치된 행렬로 읽으므로
//   C++에서 XMMatrixTranspose 로 전치해서 올림. #pragma pack_matrix(row_major)는 섞어 쓰지 않음.
//   worldInvTranspose = (W⁻¹)ᵀ 인데 전치해서 올려야 하므로 실제로 올리는 값은 W⁻¹ 임.
//
// 레지스터 배정. D3D11의 t#/s# 는 스테이지마다 번호 공간이 하나뿐이므로 셋끼리 겹치지 않게 나눔.
// D3D12 에서는 이 구간이 그대로 디스크립터 테이블 범위가 됨.
//   재질 셋:   t0..t7   s0..s3   (알베도 t0, 노멀 t1, 금속성/거칠기 t2, AO t3, 발광 t4, 재질 샘플러 s0)
//   프레임 셋: t8..t15  s4..s7   (그림자 맵 t8, 비교 샘플러 s4, 스카이박스 t9, 환경 샘플러 s5,
//                                 IBL 확산 조도 t10, IBL 반사 프리필터 t11, BRDF LUT t12)

constexpr uint32_t kCBSlotPerFrame = 0;
constexpr uint32_t kCBSlotPerObject = 1;
constexpr uint32_t kCBSlotLights = 2;
constexpr uint32_t kCBSlotMaterial = 3;
constexpr uint32_t kSRVSlotAlbedo = 0;       // t0 (재질)
constexpr uint32_t kSRVSlotNormal = 1;       // t1 (재질)
constexpr uint32_t kSRVSlotMetallicRoughness = 2; // t2 (재질, G = 거칠기, B = 금속성)
constexpr uint32_t kSRVSlotOcclusion = 3;    // t3 (재질, R = AO)
constexpr uint32_t kSRVSlotEmissive = 4;     // t4 (재질, sRGB)
constexpr uint32_t kSRVSlotSpecular = 5;     // t5 (재질, A = KHR_materials_specular 의 specularFactor 에 곱함)
constexpr uint32_t kSRVSlotSpecularColor = 6; // t6 (재질, sRGB. KHR_materials_specular 의 specularColor 에 곱함)
constexpr uint32_t kSRVSlotClearcoat = 16;   // t16 (재질, R = KHR_materials_clearcoat 의 clearcoatFactor 에 곱함). 재질 셋은 t0~t6 에 이어 프레임 셋(t8~t15)을 피해 t16 부터 씀
constexpr uint32_t kSRVSlotClearcoatRoughness = 17; // t17 (재질, G = clearcoatRoughness 에 곱함)
constexpr uint32_t kSRVSlotClearcoatNormal = 18;    // t18 (재질, 코팅 노멀 맵. 선형)
constexpr uint32_t kSRVSlotTransmission = 19;      // t19 (재질, R = KHR_materials_transmission 의 transmissionFactor 에 곱함)
constexpr uint32_t kSRVSlotShadowMap = 8;    // t8 (프레임)
constexpr uint32_t kSRVSlotEnvironment = 9;  // t9 (프레임, 스카이박스 큐브맵)
constexpr uint32_t kSRVSlotIrradiance = 10;  // t10 (프레임, IBL 확산 조도 큐브)
constexpr uint32_t kSRVSlotPrefiltered = 11; // t11 (프레임, IBL 반사 프리필터 큐브. 밉 = 거칠기)
constexpr uint32_t kSRVSlotBrdfLut = 12;     // t12 (프레임, split-sum BRDF LUT 2D)
constexpr uint32_t kSRVSlotCsmMap = 14;      // t14 (프레임, 방향광 CSM 전용 아틀라스. 후처리 셋의 t13 과는 셋이 달라 겹치지 않음)
constexpr uint32_t kSRVSlotLights = 15;      // t15 (프레임, 조명 구조체 버퍼 StructuredBuffer<LightData>)
constexpr uint32_t kSRVSlotSceneHdr = 13;   // t13 (후처리 셋, 후처리 패스의 입력 텍스처. 톤 패스는 HDR 씬, 블러·블룸은 앞 단계의 결과)
constexpr uint32_t kCBSlotPostParams = 4;    // b4 (후처리 셋, 패스마다 올림)
constexpr uint32_t kUAVSlotPostOutput = 0;   // u0 (컴퓨트 블러 셋, 출력 RWTexture2D)
constexpr uint32_t kSRVSlotBlurWeights = 20; // t20 (후처리 셋·컴퓨트 블러 셋, 가우시안 가중치 StructuredBuffer<float>. 재질 t16~t19 다음)
constexpr uint32_t kUAVSlotBlurWeights = 0;  // u0 (가중치 컴퓨트 셋, RWStructuredBuffer<float>)
constexpr uint32_t kMaxBlurRadius = 64;      // 블러 반경 상한 (픽셀). 가중치 버퍼 = kMaxBlurRadius + 1 개. 셰이더의 kMaxBlurRadius 와 같아야 함
constexpr uint32_t kCBSlotShadowPass = 5;   // b5 (프레임 셋, VS). 그림자 패스가 뷰마다 올림
constexpr uint32_t kCBSlotShadows = 6;      // b6 (프레임 셋, PS). 그림자 뷰 배열
constexpr uint32_t kCBSlotSkin = 7;         // b7 (오브젝트 셋, VS). 스키닝 조인트 행렬 팔레트
constexpr uint32_t kCBSlotParticle = 8;     // b8 (파티클 시뮬레이션 셋 CS · 그리기 셋 VS/PS). 이미터마다 한 벌, 프레임에 한 번 올림
constexpr uint32_t kSRVSlotParticles = 21;  // t21 (파티클 그리기 셋, VS. StructuredBuffer<Particle>)
constexpr uint32_t kSRVSlotParticleAlive = 22; // t22 (파티클 그리기 셋, VS. 이번 프레임 생존 목록 StructuredBuffer<uint>, 알파 이미터는 정렬된 목록)
constexpr uint32_t kSRVSlotSceneDepth = 23;    // t23 (파티클 깊이 셋 PS · 충돌 셋 CS. 씬 깊이 Texture2D<float>, DepthRead 상태)
constexpr uint32_t kCBSlotParticleCollision = 9; // b9 (파티클 충돌 셋, CS). 메인 카메라 행렬, 프레임에 한 번
// 알파 이미터 정렬 (바이토닉). Shaders/ParticleSort.hlsli 와 같음
constexpr uint32_t kSRVSlotParticleCounters = 24; // t24 (정렬 셋, CS. 개수 버퍼를 SRV 로)
constexpr uint32_t kUAVSlotSortKeys = 0;          // u0 (정렬 셋, 키 = 뷰 공간 z)
constexpr uint32_t kUAVSlotSortIndices = 1;       // u1 (정렬 셋, 파티클 번호 — 그리기 셋의 t22 가 됨)
constexpr uint32_t kCBSlotParticleSortView = 10;  // b10 (정렬 셋, 뷰마다의 view 행렬. 프레임에 한 번)
constexpr uint32_t kCBSlotParticleSortStep = 11;  // b11 (정렬 단계 셋, (k, j). 처음에 한 번 만들고 바꾸지 않음)

// 정렬 길이: capacity 이상인 가장 작은 2 의 거듭제곱 (최소 64). ParticleSort.hlsli 의 ParticleSortCount 와 같은 식.
constexpr uint32_t ParticleSortCount(uint32_t capacity)
{
	uint32_t n = 64;
	while (n < capacity) n <<= 1;
	return n;
}
// 파티클 시뮬레이션 셋 u0~u5 (Shaders/ParticleSim.hlsli 와 같은 순서)
constexpr uint32_t kUAVSlotParticles = 0;         // u0 RWStructuredBuffer<Particle>
constexpr uint32_t kUAVSlotParticleDead = 1;      // u1 사망 목록 (uint)
constexpr uint32_t kUAVSlotParticleAliveIn = 2;   // u2 지난 프레임 생존 목록 (uint)
constexpr uint32_t kUAVSlotParticleAliveOut = 3;  // u3 이번 프레임 생존 목록 (uint)
constexpr uint32_t kUAVSlotParticleCounters = 4;  // u4 개수 (uint × kParticleCounterCount)
constexpr uint32_t kUAVSlotParticleArgs = 5;      // u5 간접 인자 (RWByteAddressBuffer)
constexpr uint32_t kParticleCounterCount = 8;     // Particle.hlsli 의 kParticleCounterCount
constexpr uint32_t kParticleArgsDrawOffset = 0;      // 바이트. {정점 6, 생존 수, 0, 0} — DrawInstancedIndirect
constexpr uint32_t kParticleArgsDispatchOffset = 16; // 바이트. {그룹 수, 1, 1} — 시뮬레이션의 DispatchIndirect
constexpr uint32_t kParticleArgsSize = 32;           // 16 + 12 를 16 의 배수로
constexpr uint32_t kParticleStride = 32;    // Particle.hlsli 의 struct Particle (float3 위치 + 나이, float3 속도 + 수명)
constexpr uint32_t kMaxSkinJoints = 256;    // 팔레트 크기. 정점의 조인트 번호가 uint8 이라 이보다 클 수 없음 (Mesh.hlsli 의 MAX_JOINTS)

constexpr uint32_t kSamplerSlotPost = 6;     // s6 (후처리 셋, 선형 Clamp. 블룸 축소·확대와 썸네일이 씀. 후처리 패스는 프레임 셋을 바인딩하지 않으므로 s4~s7 과 겹치지 않음)
constexpr uint32_t kSamplerSlotMaterial = 0; // s0 (재질)
constexpr uint32_t kSamplerSlotShadow = 4;   // s4 (프레임, 비교 샘플러)
constexpr uint32_t kSamplerSlotEnvironment = 5; // s5 (프레임, 큐브맵용 선형 Clamp)



struct alignas(16) PerFrameConstants
{
	DirectX::XMFLOAT4X4 view;
	DirectX::XMFLOAT4X4 proj;
	DirectX::XMFLOAT4X4 viewProj;
	DirectX::XMFLOAT3 cameraPosition;
	float time;
	DirectX::XMFLOAT4 shadowParams;      // x = 1/맵 크기, y = 깊이 바이어스, z = 세기(0..1), w = 사용 여부
	DirectX::XMFLOAT4 debugParams;       // x = 디버그 뷰(0 조명, 1 알베도, 2 노멀, 3 깊이, 4 그림자, 5 UV, 6 금속성/거칠기, 7 AO), y = 깊이 범위
	// 환경광(IBL). x = 세기, y = 반사 큐브(t11)의 밉 수, z = 확산 조도 큐브(t10)를 읽을 밉,
	// w = 환경 큐브가 있음(1) / 없음(0, ambientColor 로 대체)
	DirectX::XMFLOAT4 environmentParams;
	// 출력. x = 노출(최종 색에 곱함), y = ACES 톤매핑(1) / 끔(0), z = BRDF LUT(t12) 있음(1) / 해석적 근사(0),
	// w = 미사용 HDR: x, y 는 톤 패스(TonemapPixelShader)가 읽음. 톤 패스의 Layout_Post 가 이 b0 를 그대로
	// 다시 씀.
	DirectX::XMFLOAT4 outputParams;
	// 노말 섀도. x = 노말맵이 그림자 UV 를 미는 배율 (에디터의 Normal offset), yzw = 미사용
	DirectX::XMFLOAT4 shadowExtra;
	// IBL 색 배율. xyz = 확산 조도·반사 프리필터에 곱할 RGB (기본 1,1,1), w = 미사용
	DirectX::XMFLOAT4 iblTint;
};

// 후처리 패스 파라미터 (b4). 패스마다 올림 — 업로드 링을 쓰므로 프레임 안에서 여러 번 올려도 됨 (b0 가
// 미리보기에서 그렇게 함). 톤 패스도 매 프레임 올리므로 효과가 전부 꺼져 있어도 D3D12 의 "이번 프레임에
// 갱신되지 않은 Dynamic CB" 경고가 나지 않음.
struct alignas(16) PostParamsConstants
{
	// x = 모드 (PS_Post: 1 블러, 2 블룸 밝기 추출+축소, 3 축소, 4 확대), y,z = 블러 방향 (텍셀 단위, 가로
	// (1,0) / 세로 (0,1)), w = 미사용
	DirectX::XMFLOAT4 pass;
	// x = 시그마(픽셀), y = 반경(픽셀, 정수로 읽음), zw = 미사용
	DirectX::XMFLOAT4 blur;
	// x = 임계값, y = knee, z = 확대 결과에 곱할 배율 (축적 확대는 1, 마지막 합성은 intensity), w = 미사용
	DirectX::XMFLOAT4 bloom;
	// xy = 입력 텍스처 크기, zw = 출력 타깃 크기 (픽셀). 톤 패스는 Load 좌표를 xy/zw 비로 늘려 읽음 (썸네일 =
	// 축소 출력)
	DirectX::XMFLOAT4 view;
	// x = GreyScale 양 (0..1), y = 1 이면 색 효과를 건너뜀 (단계별 썸네일의 "효과 전" 그림), zw = 미사용
	DirectX::XMFLOAT4 grade;
};

struct alignas(16) PerObjectConstants
{
	DirectX::XMFLOAT4X4 world;
	DirectX::XMFLOAT4X4 worldInvTranspose;
};

// b7 Skin — 스키닝 메시를 그릴 때만 올림. 앞의 n 개만 올려도 됨 (셰이더는 정점이 가리키는 조인트만 읽음).
struct alignas(16) SkinConstants
{
	DirectX::XMFLOAT4X4 joints[kMaxSkinJoints];   // 전치해서 저장
};

// 조명 하나의 GPU 배치. HLSL(Shaders/Lighting.hlsli 의 LightData)과 메모리 배치가 바이트 단위로 같아야 함.
// 씬의 Light (Scene/Light.h) 를 Renderer 가 프레임마다 이것으로 옮겨 구조체 버퍼(t15)에 올림.
//
// HLSL 패킹 규칙 (cbuffer 와 같은 순서를 지키면 구조체 버퍼에서도 같음):
//   - 16바이트 레지스터 단위로 채우며, 벡터 하나가 레지스터 경계를 넘을 수 없음.
//   - 따라서 float3 다음에는 4바이트 스칼라를 두어 정확히 16바이트를 채움.
// 아래 필드 순서는 그 규칙에 맞춰 직접 배치한 것임. 필드를 하나 옮기면 HLSL 쪽도 같이 옮겨야 함.
struct GpuLight
{
	DirectX::XMFLOAT3 position;
	float intensity;

	DirectX::XMFLOAT3 direction;   // 정규화된 진행 방향
	float range;

	DirectX::XMFLOAT3 color;
	uint32_t type;                 // LightType

	DirectX::XMFLOAT3 attenuation;
	float innerConeCos;

	float outerConeCos;
	uint32_t castShadows;
	int32_t shadowIndex;           // 이 조명의 첫 그림자 뷰 번호 (b6 의 views 인덱스). −1 = 그림자 없음. Renderer 가 프레임마다 정함
	float padding;
};
static_assert(sizeof(GpuLight) == 80, "GpuLight 는 HLSL LightData 와 같이 80바이트(16바이트 레지스터 5개)여야 함");
static_assert(offsetof(GpuLight, castShadows) == 68, "castShadows 는 HLSL LightData 의 68바이트 위치여야 함");
static_assert(offsetof(GpuLight, shadowIndex) == 72, "shadowIndex 는 HLSL LightData 의 72바이트 위치여야 함");

// b2. 조명 배열은 여기 없음 — 구조체 버퍼(t15, StructuredBuffer<LightData>)에 있고 개수만 여기 둠.
struct alignas(16) LightConstants
{
	DirectX::XMFLOAT3 ambientColor;
	uint32_t lightCount;
};

// 재질: uvScale·unlit·normalStrength 를 포함함.
// PBR. specularColor·shininess 자리(16~31)에 emissive·metallic 이, 끝의 패딩 자리에
// roughness·occlusionStrength 가 들어감. 크기는 64B 그대로.
struct alignas(16) MaterialConstants
{
	DirectX::XMFLOAT4 baseColor;       // 정점 색·텍스처에 곱해짐
	DirectX::XMFLOAT3 emissive;        // 발광 계수 (선형)
	float metallic;
	DirectX::XMFLOAT2 uvScale;         // 타일링
	float unlit;                       // 1 = 조명 없이 알베도 출력
	float normalStrength;              // 노멀 맵 xy 배율. 0 = 노멀 맵 무시
	float alphaCutoff;                 // 알파 컷아웃. 0 = 끔
	float roughness;
	float occlusionStrength;
	float ior;                         // KHR_materials_ior. 1.5 = F0 0.04
	DirectX::XMFLOAT3 specularColor;   // KHR_materials_specular. 비금속 F0 에 곱함
	float specularFactor;              // 비금속 반사 세기 = F90
	float clearcoatFactor;             // KHR_materials_clearcoat. 0 = 끔
	float clearcoatRoughness;
	float clearcoatNormalScale;
	float transmissionFactor;          // KHR_materials_transmission. 0 = 끔
	float blended;                     // 1 = 블렌딩 패스로 그리는 재질 (알파 블렌드·투과). 출력을 미리 알파를 곱한 형태로 냄
	float padding1;
	float padding2;
	float padding3;
};

struct alignas(16) ShadowPassConstants
{
	DirectX::XMFLOAT4X4 shadowViewProj;     // 지금 그리는 그림자 뷰 (전치해서 올림)
};

struct alignas(16) ShadowViewData
{
	DirectX::XMFLOAT4X4 viewProj;           // 월드 → 이 뷰의 클립 (전치해서 올림)
	DirectX::XMFLOAT4 atlasRect;            // xy = 아틀라스 UV 시작, zw = UV 크기
	DirectX::XMFLOAT4 params;               // x = NDC 깊이 바이어스, y = 텍셀의 월드 크기(직교) / 그 계수(원근, × 거리),
	// z = 1 원근 · 0 직교, w = 0 이면 t8 의 메인 아틀라스 · 1 이면 t14 의 CSM 아틀라스
};

struct alignas(16) ShadowConstants
{
	ShadowViewData views[MAX_SHADOW_VIEWS];
	DirectX::XMFLOAT4 cascadeSplits;        // 단계 7. 캐스케이드 i 가 끝나는 카메라 전방 거리
	DirectX::XMFLOAT4 cascadeOrigin;        // xyz = 캐스케이드를 계산한 카메라 위치, w = 캐스케이드 수 (0 = CSM 끔)
	DirectX::XMFLOAT4 cascadeForward;       // xyz = 그 카메라의 전방, w = 캐스케이드 경계 블렌딩 비율 (0 = 끔)
	DirectX::XMFLOAT4 atlasParams;          // x = 1/아틀라스 크기, y = PCF 여백(텍셀), z = 지역광 노멀 오프셋(텍셀, 단계 5), w = 미사용
	DirectX::XMFLOAT4 csmAtlasParams;       // xy = 1/CSM 아틀라스 (가로, 세로), zw = 미사용
};

// C++20: consteval 검사 함수. cbuffer 는 16바이트 레지스터 단위로 올라가고 D3D11 은 65,536바이트가 상한임.
// consteval 이라 런타임에는 존재하지 않는 함수임 — static_assert 안에서만 호출됨. 아래의 정확한 크기 단언은
// 그대로 두고(HLSL 과 바이트 단위로 대조하는 것이 실제 검사), 이 함수는 새 cbuffer 를 추가할 때 먼저 걸리는
// 일반 규칙임.
consteval bool IsValidConstantBufferSize(size_t bytes)
{
	return bytes > 0 && bytes % 16 == 0 && bytes <= 65536;
}
// b8: 파티클 이미터 하나 (Particle.hlsli 의 cbuffer ParticleEmitter 와 같은 순서). 이미터마다 Dynamic 버퍼 한 벌이고
// 프레임에 한 번 올림 — 시뮬레이션(CS)과 뷰마다의 그리기(VS/PS)가 같은 값을 읽음. 공용 버퍼를 단계 사이에 다시
// 올리면 D3D12 루트 CBV 주소가 바뀌는 순서 문제가 생기므로 나누지 않음.
struct alignas(16) ParticleEmitterConstants
{
	DirectX::XMFLOAT3 position;    // 이미터 월드 위치 (오브젝트 월드 행렬의 원점). Emit: 생성 위치의 중심
	float deltaTime;               // 이번 프레임 시뮬레이션 시간 (초). --particle-step 이면 고정, 아니면 경과 시간을 [0, 0.1] 로 자름
	DirectX::XMFLOAT3 direction;   // 방출 방향 (오브젝트 로컬 +Y 의 월드 방향, 정규화). Emit: 원뿔의 축
	float spreadCos;               // cos(spreadAngle). 1 = 한 방향, −1 = 모든 방향
	DirectX::XMFLOAT3 color;       // 선형 RGB × intensity (HDR). VS 의 정점 색
	float lifetime;                // 수명 (초, ≥ 0.01). 태어날 때 파티클에 복사
	DirectX::XMFLOAT3 gravity;     // 가속도 (m/s², 월드). Emit·Sim
	float emitRadius;              // 생성 위치가 퍼지는 구의 반지름 (m)
	float startSpeed;              // 방출 속력 (m/s)
	float speedVariation;          // 0..1. 속력 = startSpeed × (1 − variation × 난수)
	float startSize;               // 태어날 때 쿼드 한 변 (m)
	float endSize;                 // 수명 끝의 쿼드 한 변 (m). 수명 진행률로 보간
	uint32_t capacity;             // 파티클 버퍼 칸 수 (maxParticles 를 [1, 262144] 로 자름). Reset 범위, 정렬 길이 계산
	uint32_t spawnCount;           // 이번 프레임에 태어나길 원하는 수 (0 ≤ spawnCount ≤ capacity). 사망 목록이 모자라면 GPU 가 줄임
	uint32_t seed;                 // 난수 씨앗 (이미터의 프레임 번호). 같은 씨앗·같은 dt 면 같은 파티클이 태어남
	uint32_t collide;              // 1 이면 깊이 버퍼 충돌
	float softDistance;            // 소프트 파티클 거리 (m). 0 = 끔
	float bounce;                  // 충돌 반발 계수 0..1 (0 미끄러짐, 1 완전 탄성)
	float opacity;                 // 알파 모드의 불투명도 배율 0..1 (PS)
	uint32_t blendMode;            // 0 가산 (PS 가 알파 0), 1 알파 (프리멀티플라이드, 정렬)
	DirectX::XMFLOAT3 endTint;     // 수명 끝의 색 배율 (1 → endTint 선형 보간)
	float padding;                 // 16바이트 칸 맞춤
};

// b9: 파티클 충돌 (메인 뷰의 깊이로 화면 공간 충돌). 행렬은 전치해서 올림 (PerFrame 과 같음).
struct alignas(16) ParticleCollisionConstants
{
	DirectX::XMFLOAT4X4 viewProj;
	DirectX::XMFLOAT4X4 invViewProj;
	float depthA;                  // 투영 행렬 _33 (행 벡터 기준 [2][2]). 깊이 → 뷰 z 복원
	float depthB;                  // _43 ([3][2])
	float perspective;             // 1 원근, 0 직교
	float thickness;               // 표면 뒤 이 거리(m) 안에 들어온 파티클만 충돌로 봄 (얇은 물체 뒤로 지나가는 것은 통과)
	uint32_t width;                // 깊이 텍스처 크기
	uint32_t height;
	uint32_t padding[2];
};

// b10: 정렬 키를 만들 뷰의 view 행렬 (전치). 뷰마다 Dynamic 버퍼 하나.
struct alignas(16) ParticleSortViewConstants
{
	DirectX::XMFLOAT4X4 view;
};

// b11: 바이토닉 정렬 한 단계. 단계마다 Default 상수 버퍼를 초기 데이터로 한 번 만듦 — 디스패치마다 다른 값을 Dynamic
// 버퍼 하나에 다시 올릴 수 없음 (D3D12 의 프레임당 한 번 규칙). 크기를 256 으로 맞춤: D3D12 백엔드가 상수 버퍼 크기를
// 256 의 배수로 올리고 그 크기만큼 초기 데이터를 복사함.
struct alignas(16) ParticleSortStepConstants
{
	uint32_t k;
	uint32_t j;
	uint32_t padding[62];
};

static_assert(IsValidConstantBufferSize(sizeof(PerFrameConstants)), "PerFrameConstants 는 16의 배수여야 함");
static_assert(sizeof(ParticleEmitterConstants) == 128, "ParticleEmitterConstants: 16 * 8");
static_assert(offsetof(ParticleEmitterConstants, softDistance) == 96, "ParticleEmitterConstants.softDistance");
static_assert(sizeof(ParticleCollisionConstants) == 160, "ParticleCollisionConstants: 64 * 2 + 16 * 2");
static_assert(sizeof(ParticleSortViewConstants) == 64, "ParticleSortViewConstants");
static_assert(sizeof(ParticleSortStepConstants) == 256, "ParticleSortStepConstants: D3D12 가 CB 를 256 으로 올리므로 초기 데이터도 256");
static_assert(offsetof(ParticleEmitterConstants, startSpeed) == 64, "ParticleEmitterConstants.startSpeed");
static_assert(offsetof(ParticleEmitterConstants, capacity) == 80, "ParticleEmitterConstants.capacity");
static_assert(offsetof(ParticleEmitterConstants, seed) == 88, "ParticleEmitterConstants.seed");
static_assert(IsValidConstantBufferSize(sizeof(PerObjectConstants)), "PerObjectConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(LightConstants)), "LightConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(MaterialConstants)), "MaterialConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(PostParamsConstants)), "PostParamsConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(ShadowPassConstants)), "ShadowPassConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(ShadowConstants)), "ShadowConstants 는 16의 배수여야 함");
static_assert(sizeof(PostParamsConstants) == 80, "PostParamsConstants: 16 * 5");

// lightViewProj 삭제 후: view/proj/viewProj(64*3) + 16 * 6
static_assert(sizeof(PerFrameConstants) == 304, "PerFrameConstants: 64*3 + 16 * 7");
static_assert(offsetof(PerFrameConstants, cameraPosition) == 192, "PerFrameConstants.cameraPosition");
static_assert(offsetof(PerFrameConstants, time) == 204, "PerFrameConstants.time");
static_assert(offsetof(PerFrameConstants, shadowParams) == 208, "PerFrameConstants.shadowParams");
static_assert(offsetof(PerFrameConstants, debugParams) == 224, "PerFrameConstants.debugParams");
static_assert(offsetof(PerFrameConstants, environmentParams) == 240, "PerFrameConstants.environmentParams");
static_assert(offsetof(PerFrameConstants, outputParams) == 256, "PerFrameConstants.outputParams");
static_assert(offsetof(PerFrameConstants, shadowExtra) == 272, "PerFrameConstants.shadowExtra");
static_assert(offsetof(PerFrameConstants, iblTint) == 288, "PerFrameConstants.iblTint");

static_assert(sizeof(PerObjectConstants) == 128, "PerObjectConstants: 64*2");
static_assert(IsValidConstantBufferSize(sizeof(SkinConstants)), "SkinConstants 는 16의 배수여야 함");
static_assert(sizeof(SkinConstants) == 64 * kMaxSkinJoints, "SkinConstants: 64 * 조인트 수 (HLSL cbuffer Skin)");
static_assert(offsetof(PerObjectConstants, worldInvTranspose) == 64, "PerObjectConstants.worldInvTranspose");

static_assert(sizeof(LightConstants) == 16, "LightConstants: 16");
static_assert(offsetof(LightConstants, lightCount) == 12, "LightConstants.lightCount");

static_assert(sizeof(MaterialConstants) == 112, "MaterialConstants: 16 * 7");
static_assert(offsetof(MaterialConstants, emissive) == 16, "MaterialConstants.emissive");
static_assert(offsetof(MaterialConstants, metallic) == 28, "MaterialConstants.metallic");
static_assert(offsetof(MaterialConstants, uvScale) == 32, "MaterialConstants.uvScale");
static_assert(offsetof(MaterialConstants, unlit) == 40, "MaterialConstants.unlit");
static_assert(offsetof(MaterialConstants, normalStrength) == 44, "MaterialConstants.normalStrength");
static_assert(offsetof(MaterialConstants, alphaCutoff) == 48, "MaterialConstants.alphaCutoff");
static_assert(offsetof(MaterialConstants, roughness) == 52, "MaterialConstants.roughness");
static_assert(offsetof(MaterialConstants, occlusionStrength) == 56, "MaterialConstants.occlusionStrength");
static_assert(offsetof(MaterialConstants, ior) == 60, "MaterialConstants.ior");
static_assert(offsetof(MaterialConstants, specularColor) == 64, "MaterialConstants.specularColor");
static_assert(offsetof(MaterialConstants, specularFactor) == 76, "MaterialConstants.specularFactor");
static_assert(offsetof(MaterialConstants, clearcoatFactor) == 80, "MaterialConstants.clearcoatFactor");
static_assert(offsetof(MaterialConstants, transmissionFactor) == 92, "MaterialConstants.transmissionFactor");
static_assert(offsetof(MaterialConstants, blended) == 96, "MaterialConstants.blended");

static_assert(sizeof(ShadowPassConstants) == 64, "ShadowPassConstants: 64");

static_assert(sizeof(ShadowViewData) == 96, "ShadowViewData: 64 + 16 + 16");
static_assert(offsetof(ShadowViewData, atlasRect) == 64, "ShadowViewData.atlasRect");
static_assert(offsetof(ShadowViewData, params) == 80, "ShadowViewData.params");

static_assert(sizeof(ShadowConstants) == 2000, "ShadowConstants: 96*20 + 16*5");
static_assert(offsetof(ShadowConstants, cascadeSplits) == 1920, "ShadowConstants.cascadeSplits");
static_assert(offsetof(ShadowConstants, cascadeOrigin) == 1936, "ShadowConstants.cascadeOrigin");
static_assert(offsetof(ShadowConstants, cascadeForward) == 1952, "ShadowConstants.cascadeForward");
static_assert(offsetof(ShadowConstants, atlasParams) == 1968, "ShadowConstants.atlasParams");
static_assert(offsetof(ShadowConstants, csmAtlasParams) == 1984, "ShadowConstants.csmAtlasParams");

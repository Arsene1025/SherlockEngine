#pragma once
#include <cstdint>
#include <cstddef>
#include <DirectXMath.h>
#include "Scene/Light.h"

// 상수버퍼 레이아웃. Shaders/Common.hlsli 의 cbuffer 네 개와 바이트 단위로 같아야 함.
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
constexpr uint32_t kSRVSlotShadowMap = 8;    // t8 (프레임)
constexpr uint32_t kSRVSlotEnvironment = 9;  // t9 (프레임, 스카이박스 큐브맵)
constexpr uint32_t kSRVSlotIrradiance = 10;  // t10 (프레임, IBL 확산 조도 큐브)
constexpr uint32_t kSRVSlotPrefiltered = 11; // t11 (프레임, IBL 반사 프리필터 큐브. 밉 = 거칠기)
constexpr uint32_t kSRVSlotBrdfLut = 12;     // t12 (프레임, split-sum BRDF LUT 2D)
constexpr uint32_t kSRVSlotCsmMap = 14;      // t14 (프레임, 방향광 CSM 전용 아틀라스. 후처리 셋의 t13 과는 셋이 달라 겹치지 않음)
constexpr uint32_t kSRVSlotLights = 15;      // t15 (프레임, 조명 구조체 버퍼 StructuredBuffer<LightData>)
constexpr uint32_t kSRVSlotSceneHdr = 13;   // t13 (후처리 셋, 후처리 패스의 입력 텍스처. 톤 패스는 HDR 씬, 블러·블룸은 앞 단계의 결과)
constexpr uint32_t kCBSlotPostParams = 4;    // b4 (후처리 셋, 패스마다 올림)
constexpr uint32_t kCBSlotShadowPass = 5;   // b5 (프레임 셋, VS). 그림자 패스가 뷰마다 올림
constexpr uint32_t kCBSlotShadows = 6;      // b6 (프레임 셋, PS). 그림자 뷰 배열
constexpr uint32_t kCBSlotSkin = 7;         // b7 (오브젝트 셋, VS). 스키닝 조인트 행렬 팔레트
constexpr uint32_t kMaxSkinJoints = 256;    // 팔레트 크기. 정점의 조인트 번호가 uint8 이라 이보다 클 수 없음 (Common.hlsli 의 MAX_JOINTS)

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

// 조명 하나의 GPU 배치. HLSL(Shaders/Common.hlsli 의 LightData)과 메모리 배치가 바이트 단위로 같아야 함.
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
	float padding;
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
static_assert(IsValidConstantBufferSize(sizeof(PerFrameConstants)), "PerFrameConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(PerObjectConstants)), "PerObjectConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(LightConstants)), "LightConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(MaterialConstants)), "MaterialConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(PostParamsConstants)), "PostParamsConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(ShadowPassConstants)), "ShadowPassConstants 는 16의 배수여야 함");
static_assert(IsValidConstantBufferSize(sizeof(ShadowConstants)), "ShadowConstants 는 16의 배수여야 함");
static_assert(sizeof(PostParamsConstants) == 80, "PostParamsConstants: 16 * 5");

// lightViewProj 삭제 후: view/proj/viewProj(64*3) + 16 * 6
static_assert(sizeof(PerFrameConstants) == 288, "PerFrameConstants: 64*3 + 16 * 6");
static_assert(offsetof(PerFrameConstants, cameraPosition) == 192, "PerFrameConstants.cameraPosition");
static_assert(offsetof(PerFrameConstants, time) == 204, "PerFrameConstants.time");
static_assert(offsetof(PerFrameConstants, shadowParams) == 208, "PerFrameConstants.shadowParams");
static_assert(offsetof(PerFrameConstants, debugParams) == 224, "PerFrameConstants.debugParams");
static_assert(offsetof(PerFrameConstants, environmentParams) == 240, "PerFrameConstants.environmentParams");
static_assert(offsetof(PerFrameConstants, outputParams) == 256, "PerFrameConstants.outputParams");
static_assert(offsetof(PerFrameConstants, shadowExtra) == 272, "PerFrameConstants.shadowExtra");

static_assert(sizeof(PerObjectConstants) == 128, "PerObjectConstants: 64*2");
static_assert(IsValidConstantBufferSize(sizeof(SkinConstants)), "SkinConstants 는 16의 배수여야 함");
static_assert(sizeof(SkinConstants) == 64 * kMaxSkinJoints, "SkinConstants: 64 * 조인트 수 (HLSL cbuffer Skin)");
static_assert(offsetof(PerObjectConstants, worldInvTranspose) == 64, "PerObjectConstants.worldInvTranspose");

static_assert(sizeof(LightConstants) == 16, "LightConstants: 16");
static_assert(offsetof(LightConstants, lightCount) == 12, "LightConstants.lightCount");

static_assert(sizeof(MaterialConstants) == 64, "MaterialConstants: 16 + 16 + 16 + 16");
static_assert(offsetof(MaterialConstants, emissive) == 16, "MaterialConstants.emissive");
static_assert(offsetof(MaterialConstants, metallic) == 28, "MaterialConstants.metallic");
static_assert(offsetof(MaterialConstants, uvScale) == 32, "MaterialConstants.uvScale");
static_assert(offsetof(MaterialConstants, unlit) == 40, "MaterialConstants.unlit");
static_assert(offsetof(MaterialConstants, normalStrength) == 44, "MaterialConstants.normalStrength");
static_assert(offsetof(MaterialConstants, alphaCutoff) == 48, "MaterialConstants.alphaCutoff");
static_assert(offsetof(MaterialConstants, roughness) == 52, "MaterialConstants.roughness");
static_assert(offsetof(MaterialConstants, occlusionStrength) == 56, "MaterialConstants.occlusionStrength");

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

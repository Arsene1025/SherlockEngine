#pragma once
#include <cstdint>
#include <cstddef>
#include <DirectXMath.h>
#include "Scene/Light.h"

// 상수버퍼 레이아웃. Shaders/Common.hlsli 의 cbuffer 네 개와 바이트 단위로 같아야 함.
//
// 갱신 빈도별로 나눔 (rendering-analysis D2):
//   b0 PerFrame  — 프레임에 한 번. 뷰·투영·광원 ViewProj·카메라 위치·시간·그림자 파라미터. VS와 PS 모두.
//   b1 PerObject — 드로우마다. 월드 행렬과 노멀 변환용 역전치.       VS만.
//   b2 Lights    — 프레임에 한 번(조명이 바뀔 때만이어도 됨).           PS만.
//   b3 Material  — 재질마다(4단계). 기본색·스페큘러·광택·UV 배율·노멀 맵 세기. VS(uvScale)와 PS.
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
// 행렬 관례:
//   HLSL은 기본적으로 column_major 로 읽고, 셰이더는 mul(v, M) 행벡터 곱을 씀.
//   DirectXMath 행렬(행우선 메모리)을 그대로 올리면 HLSL이 전치된 행렬로 읽으므로
//   C++에서 XMMatrixTranspose 로 전치해서 올림. #pragma pack_matrix(row_major)는 섞어 쓰지 않음.
//   worldInvTranspose = (W⁻¹)ᵀ 인데 전치해서 올려야 하므로 실제로 올리는 값은 W⁻¹ 임.
//
// 레지스터 배정 (6단계). D3D11의 t#/s# 는 스테이지마다 번호 공간이 하나뿐이므로 셋끼리 겹치지 않게 나눔.
// D3D12 에서는 이 구간이 그대로 디스크립터 테이블 범위가 됨.
//   재질 셋:   t0..t7   s0..s3   (알베도 t0, 노멀 t1, 재질 샘플러 s0)
//   프레임 셋: t8..t15  s4..s7   (그림자 맵 t8, 비교 샘플러 s4)

constexpr uint32_t kCBSlotPerFrame = 0;
constexpr uint32_t kCBSlotPerObject = 1;
constexpr uint32_t kCBSlotLights = 2;
constexpr uint32_t kCBSlotMaterial = 3;
constexpr uint32_t kSRVSlotAlbedo = 0;       // t0 (재질)
constexpr uint32_t kSRVSlotNormal = 1;       // t1 (재질)
constexpr uint32_t kSRVSlotShadowMap = 8;    // t8 (프레임)
constexpr uint32_t kSamplerSlotMaterial = 0; // s0 (재질)
constexpr uint32_t kSamplerSlotShadow = 4;   // s4 (프레임, 비교 샘플러)

struct alignas(16) PerFrameConstants
{
	DirectX::XMFLOAT4X4 view;
	DirectX::XMFLOAT4X4 proj;
	DirectX::XMFLOAT4X4 viewProj;
	DirectX::XMFLOAT4X4 lightViewProj;   // 6단계: 방향광 0의 그림자 행렬 (월드 → 광원 클립)
	DirectX::XMFLOAT3 cameraPosition;
	float time;
	DirectX::XMFLOAT4 shadowParams;      // x = 1/맵 크기, y = 깊이 바이어스, z = 세기(0..1), w = 사용 여부
	DirectX::XMFLOAT4 debugParams;       // 11단계: x = 디버그 뷰(0 조명, 1 알베도, 2 노멀, 3 깊이, 4 그림자, 5 UV), y = 깊이 범위
};

struct alignas(16) PerObjectConstants
{
	DirectX::XMFLOAT4X4 world;
	DirectX::XMFLOAT4X4 worldInvTranspose;
};

struct alignas(16) LightConstants
{
	LightData lights[MAX_LIGHTS];
	DirectX::XMFLOAT3 ambientColor;
	uint32_t lightCount;
};

// 4단계: 재질. 5단계: uvScale·unlit. 6단계: normalStrength.
struct alignas(16) MaterialConstants
{
	DirectX::XMFLOAT4 baseColor;       // 정점 색·텍스처에 곱해짐
	DirectX::XMFLOAT3 specularColor;
	float shininess;
	DirectX::XMFLOAT2 uvScale;         // 타일링
	float unlit;                       // 1 = 조명 없이 알베도 출력
	float normalStrength;              // 노멀 맵 xy 배율. 0 = 노멀 맵 무시
	float alphaCutoff;                 // 9단계: 알파 컷아웃. 0 = 끔
	DirectX::XMFLOAT3 padding;
};

static_assert(sizeof(PerFrameConstants) == 304, "PerFrameConstants: 64*4 + 16 + 16 + 16");
static_assert(offsetof(PerFrameConstants, debugParams) == 288, "PerFrameConstants.debugParams");
static_assert(offsetof(PerFrameConstants, lightViewProj) == 192, "PerFrameConstants.lightViewProj");
static_assert(offsetof(PerFrameConstants, cameraPosition) == 256, "PerFrameConstants.cameraPosition");
static_assert(offsetof(PerFrameConstants, time) == 268, "PerFrameConstants.time");
static_assert(offsetof(PerFrameConstants, shadowParams) == 272, "PerFrameConstants.shadowParams");

static_assert(sizeof(PerObjectConstants) == 128, "PerObjectConstants: 64*2");
static_assert(offsetof(PerObjectConstants, worldInvTranspose) == 64, "PerObjectConstants.worldInvTranspose");

static_assert(sizeof(LightConstants) == 656, "LightConstants: 80*8 + 16");
static_assert(offsetof(LightConstants, ambientColor) == 640, "LightConstants.ambientColor");
static_assert(offsetof(LightConstants, lightCount) == 652, "LightConstants.lightCount");

static_assert(sizeof(MaterialConstants) == 64, "MaterialConstants: 16 + 16 + 16 + 16");
static_assert(offsetof(MaterialConstants, specularColor) == 16, "MaterialConstants.specularColor");
static_assert(offsetof(MaterialConstants, shininess) == 28, "MaterialConstants.shininess");
static_assert(offsetof(MaterialConstants, uvScale) == 32, "MaterialConstants.uvScale");
static_assert(offsetof(MaterialConstants, unlit) == 40, "MaterialConstants.unlit");
static_assert(offsetof(MaterialConstants, normalStrength) == 44, "MaterialConstants.normalStrength");
static_assert(offsetof(MaterialConstants, alphaCutoff) == 48, "MaterialConstants.alphaCutoff");

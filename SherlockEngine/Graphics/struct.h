#pragma once
#include <windows.h>       // UINT
#include <DirectXMath.h>   // DirectX::XMMATRIX, DirectX::XMFLOAT3
#include "Graphics/enum.h"          // LightType, MAX_LIGHTS

// VERTEX는 Graphics/VertexTypes.h로 옮겼다 (정점 레이아웃 기술과 함께 둔다).

struct ConstBuffer
{
	DirectX::XMMATRIX mWorld;
	DirectX::XMMATRIX mWorldInverseTranspose;
	DirectX::XMMATRIX mView;
	DirectX::XMMATRIX mProj;
	DirectX::XMMATRIX mWVP;
};



//16바이트 정렬
struct alignas(16) LightData
{
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0.0f, 10.0f, -10.0f);
	float intensity = 1.0f;

	// 빛이 진행하는 방향. 셰이더의 표면 방향 벡터 L은 반대 방향을 사용
	DirectX::XMFLOAT3 direction = DirectX::XMFLOAT3(0.0f, -0.4472136f, 0.8944272f);
	float range = 50.0f;

	DirectX::XMFLOAT3 color = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	UINT type = static_cast<UINT>(LightType::Directional);

	// constant, linear, quadratic attenuation
	DirectX::XMFLOAT3 attenuation = DirectX::XMFLOAT3(1.0f, 0.045f, 0.0075f);
	float innerConeCos = 0.9238795f; // cos(22.5 degrees)

	float outerConeCos = 0.8660254f; // cos(30 degrees)
	DirectX::XMFLOAT3 padding = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
};

struct alignas(16) LightBuffer
{
	LightData lights[MAX_LIGHTS];

	DirectX::XMFLOAT3 cameraPosition = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	UINT lightCount = 0;

	DirectX::XMFLOAT3 ambientColor = DirectX::XMFLOAT3(0.12f, 0.12f, 0.12f);
	float shininess = 32.0f;

	DirectX::XMFLOAT3 specularColor = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	float padding = 0.0f;
};

static_assert(sizeof(LightData) % 16 == 0, "[경고] LightData must be 16-byte aligned for HLSL.");
static_assert(sizeof(LightBuffer) % 16 == 0, "[경고] LightBuffer must be 16-byte aligned for HLSL.");

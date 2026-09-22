#pragma once
#include <cstdint>
#include <DirectXMath.h>

// 조명 데이터. C++와 HLSL(Shaders/Common.hlsli의 LightData)이 바이트 단위로 같아야 한다.
//
// HLSL 상수버퍼 패킹 규칙:
//   - 16바이트 레지스터 단위로 채우며, 벡터 하나가 레지스터 경계를 넘을 수 없다.
//   - 따라서 float3 다음에는 4바이트 스칼라를 두어 정확히 16을 채운다.
//   - 구조체 배열의 각 원소는 16바이트 경계에서 시작한다.
// 아래 순서는 그 규칙을 손으로 맞춘 것이다. 필드를 하나 옮기면 HLSL도 같이 옮겨야 한다.

enum class LightType : uint32_t
{
	Directional = 0,
	Point = 1,
	Spot = 2
};

constexpr uint32_t MAX_LIGHTS = 8;

struct alignas(16) LightData
{
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0.0f, 10.0f, -10.0f);
	float intensity = 1.0f;

	// 빛이 진행하는 방향. 셰이더의 표면 방향 벡터 L은 반대 방향을 사용
	DirectX::XMFLOAT3 direction = DirectX::XMFLOAT3(0.0f, -0.4472136f, 0.8944272f);
	float range = 50.0f;

	DirectX::XMFLOAT3 color = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	uint32_t type = static_cast<uint32_t>(LightType::Directional);

	// constant, linear, quadratic attenuation
	DirectX::XMFLOAT3 attenuation = DirectX::XMFLOAT3(1.0f, 0.045f, 0.0075f);
	float innerConeCos = 0.9238795f; // cos(22.5 degrees)

	float outerConeCos = 0.8660254f; // cos(30 degrees)
	DirectX::XMFLOAT3 padding = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
};

static_assert(sizeof(LightData) == 80, "LightData는 HLSL과 같이 80바이트(16바이트 레지스터 5개)여야 한다.");

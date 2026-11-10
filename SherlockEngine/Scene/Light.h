#pragma once
#include <cstdint>
#include <DirectXMath.h>

// 씬이 들고 있는 조명. 편집·저장되는 값만 가짐 — GPU 로 올리는 배치(패딩, 그림자 뷰 번호)는 여기 없음.
// Renderer 가 프레임마다 이것을 GpuLight (Graphics/ShaderConstants.h, HLSL 의 LightData 와 같은 80바이트)로
// 옮겨 구조체 버퍼에 올림. 그래서 필드를 더하거나 순서를 바꿔도 HLSL 패킹과 무관하고, 조명 수에도 상한이 없음.

enum class LightType : uint32_t
{
	Directional = 0,
	Point = 1,
	Spot = 2
};

struct Light
{
	LightType type = LightType::Directional;
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0.0f, 10.0f, -10.0f);
	// 빛이 진행하는 방향. 셰이더의 L(표면에서 광원을 향하는 벡터)은 이와 반대 방향을 사용함. 정규화돼 있지
	// 않아도 됨 (Renderer 가 올릴 때 정규화함).
	DirectX::XMFLOAT3 direction = DirectX::XMFLOAT3(0.0f, -0.4472136f, 0.8944272f);
	DirectX::XMFLOAT3 color = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	float intensity = 1.0f;
	float range = 50.0f;
	// constant, linear, quadratic attenuation
	DirectX::XMFLOAT3 attenuation = DirectX::XMFLOAT3(1.0f, 0.045f, 0.0075f);
	float innerConeCos = 0.9238795f; // cos(22.5 degrees)
	float outerConeCos = 0.8660254f; // cos(30 degrees)
	bool castShadows = true;
};

// 그림자 뷰(아틀라스 타일) 수의 상한. 방향광 4(캐스케이드) + 지역광 타일 16 = 20.
constexpr uint32_t MAX_SHADOW_VIEWS = 20;

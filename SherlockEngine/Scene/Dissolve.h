#pragma once
#include "Core/EngineApi.h"   // SHERLOCK_API (엔진 DLL 내보내기)
#include "Scene/Behaviour.h"
#include <DirectXMath.h>

// 오브젝트별 디졸브 (docs/plan/object-highlight-record.html 7장). 노이즈가 문턱보다 작은 픽셀을 버려 몸이 타 들어가듯 사라지게 함.
// 진행도·경계는 오브젝트별(이 컴포넌트 → 드로우마다 올리는 b1 PerObject), 노이즈 무늬는 재질별(Material::dissolveNoiseTexture,
// 비면 엔진 기본 노이즈 builtin:noise). 상용 엔진의 "재질 파라미터(기본값 있음) + 인스턴스별 값" 구조와 같음.
//
//   amount  0 = 끔 (컴포넌트가 없는 것과 같은 화면, 그림자도 PS 없는 빠른 경로), 1 = 완전히 사라짐
//   edge    문턱 바로 위 노이즈 띠(폭 edgeWidth, 노이즈 값 단위)에 edgeColor × edgeIntensity 를 발광으로 더함 (타는 테두리)
//
// 그림자: amount > 0 인 동안 그림자 패스도 알파 테스트 변형(Shaders/ShadowAlpha.hlsli)으로 그려서 그림자도 같은 무늬로 사라짐.
// 계층: Highlight 와 같음 — 자기 오브젝트와 모든 자손에 적용, 자손이 자기 Dissolve 를 가지면 가까운 쪽이 이김, 꺼진 것은 없는 것.
// 미리 붙여 둬도 되고(amount 0 은 비용이 거의 없음) 재생 중에 스크립트가 AddBehaviour 로 붙여도 됨. 값만 들고 있으므로(Update 없음)
// 진행은 스크립트가 amount 를 올려서 만듦 (예: SherlockTestScenes/Scripts/Tests/DissolveOut).
class SHERLOCK_API Dissolve : public Behaviour
{
public:
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& visitor) override;

	float amount = 0.0f;                                                       // [0, 1]
	float edgeWidth = 0.05f;                                                   // ≥ 0. 0 이면 경계 빛 없음
	DirectX::XMFLOAT4 edgeColor = DirectX::XMFLOAT4(1.0f, 0.45f, 0.1f, 1.0f);  // rgb 만 씀 (선형)
	float edgeIntensity = 4.0f;                                                // ≥ 0. HDR 배율
};

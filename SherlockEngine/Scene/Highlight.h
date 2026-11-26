#pragma once
#include "Core/EngineApi.h"   // SHERLOCK_API (엔진 DLL 내보내기)
#include "Scene/Behaviour.h"
#include <DirectXMath.h>

// 오브젝트별 표면 강조 (docs/plan/object-highlight-record.html). 피격 번쩍임·상태 표시처럼 재질은 그대로 공유하면서 이 오브젝트만
// 색을 바꿀 때 씀. 값은 드로우마다 올리는 b1 PerObject 에 실려 PS 가 읽으므로 재질 복사본도, 셰이더 변형(PSO)도 늘지 않음.
//
// 세 가지 효과 (모두 0 이면 꺼짐 — 컴포넌트가 없는 것과 같은 화면):
//   tint   기본색(알베도)을 tintColor 쪽으로 섞음. 조명을 받은 색이 됨. 금속 F0 도 이 색을 따라감 (금속은 확산이 없어서)
//   flash  발광에 flashColor × flashIntensity 를 더함. 조명과 상관없이 밝아짐 (HDR, 1 을 넘어도 됨)
//   rim    가장자리 빛. (1 − N·V)^rimPower × rimColor × rimIntensity 를 발광에 더함. 실루엣이 빛남
//
// 계층: 이 컴포넌트는 자기 오브젝트와 모든 자손에 적용됨 (모델은 루트 아래 자식 메시로 들어오므로). 자손이 자기 Highlight 를
// 가지면 그 아래로는 가까운 쪽이 이김. 꺼진(enabled = false) 컴포넌트는 없는 것으로 봄.
//
// 이 컴포넌트는 값만 들고 있음 (Update 없음). 피격 연출은 스크립트가 값을 시간에 따라 바꿔서 만듦
// (예: SherlockTestScenes/Scripts/Tests/HitFlash). 색은 재질의 Base color 처럼 선형 값 그대로 씀.
class SHERLOCK_API Highlight : public Behaviour
{
public:
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& visitor) override;

	DirectX::XMFLOAT4 tintColor = DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);    // rgb 만 씀 (a 무시)
	float tintAmount = 0.0f;                                                    // [0, 1]. 0 = 원래 색, 1 = tintColor
	DirectX::XMFLOAT4 flashColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);   // rgb 만 씀
	float flashIntensity = 0.0f;                                                // ≥ 0. 발광에 더하는 배율
	DirectX::XMFLOAT4 rimColor = DirectX::XMFLOAT4(1.0f, 0.2f, 0.1f, 1.0f);     // rgb 만 씀
	float rimIntensity = 0.0f;                                                  // ≥ 0
	float rimPower = 3.0f;                                                      // ≥ 0.1. 클수록 테두리가 얇음
};

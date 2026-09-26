#pragma once
#include "Scene/Script.h"

// 예제 스크립트 2: 가장 짧은 스크립트. 매 프레임 degreesPerSecond 만큼 회전함 — 예전 "Spin" 컴포넌트를 Transform::Rotate 호출 한 줄로 대체한 것.
// 선언은 여기(.h), 구현은 Rotator.cpp (언리얼식). 다른 스크립트는 #include "Rotator.h" 뒤 GetBehaviour<Rotator>() 로 이 클래스를 사용함
// (PlayerController 가 R 키로 Toggle 을 호출함).
class Rotator : public Script
{
public:
	const char* GetTypeName() const override;   // SHERLOCK_SCRIPT 가 정의함 (.cpp)
	void Reflect(PropertyVisitor& v) override;
	void Update(float dt) override;

	// 다른 스크립트가 부르는 API
	void Toggle() { enabled = !enabled; }
	void SetSpeed(float degreesPerSecondY) { degreesPerSecond.y = degreesPerSecondY; }

	DirectX::XMFLOAT3 degreesPerSecond = DirectX::XMFLOAT3(0.0f, 60.0f, 0.0f);
};

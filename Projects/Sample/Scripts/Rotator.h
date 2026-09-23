#pragma once
#include "Scene/Script.h"

// 예제 스크립트 2: 가장 짧은 스크립트. 매 프레임 degreesPerSecond 만큼 회전한다 — 옛 "Spin" 컴포넌트가 Transform::Rotate 호출 한 줄로 바뀐 것.
// 선언은 여기(.h), 구현은 Rotator.cpp (언리얼식). 다른 스크립트는 #include "Rotator.h" 뒤 GetBehaviour<Rotator>() 로 이 클래스를 부른다
// (PlayerController 가 R 키로 Toggle 을 부른다).
class Rotator : public Script
{
public:
	const char* GetTypeName() const override;   // SHERLOCK_SCRIPT 가 정의한다 (.cpp)
	void Reflect(PropertyVisitor& v) override;
	void Update(float dt) override;

	// 다른 스크립트가 부르는 API
	void Toggle() { enabled = !enabled; }
	void SetSpeed(float degreesPerSecondY) { degreesPerSecond.y = degreesPerSecondY; }

	DirectX::XMFLOAT3 degreesPerSecond = DirectX::XMFLOAT3(0.0f, 60.0f, 0.0f);
};

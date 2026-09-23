#pragma once
#include "Scene/Script.h"

// Test — 에디터의 New Script 로 만든 스크립트 (유니티 MonoBehaviour / 언리얼 컴포넌트).
// 선언은 여기, 구현은 Test.cpp. 다른 스크립트가 #include "Test.h" 뒤 GetBehaviour<Test>() 로 이 클래스를 부를 수 있다.
// 다른 스크립트를 참조할 때는 여기서 전방 선언(class Other;)만 하고 .cpp 에서 include 한다 (순환 include 방지).
class Test : public Script
{
public:
	const char* GetTypeName() const override;   // SHERLOCK_SCRIPT 가 정의한다 (.cpp)
	void Reflect(PropertyVisitor& v) override;  // Inspector 에 보이고 씬에 저장되는 필드
	void Start() override;                       // 재생 시작 때 한 번
	void Update(float dt) override;              // 매 프레임 (dt 초)

	float speed = 1.0f;
};

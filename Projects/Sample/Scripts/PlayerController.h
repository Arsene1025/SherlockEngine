#pragma once
#include "Scene/Script.h"

class Rotator;   // 다른 스크립트 참조는 헤더에서 전방 선언, .cpp 에서 include (순환 include 방지)

// 예제 스크립트 1: 플레이어. 방향키로 XZ 이동(Shift 가속), Space 로 점프(같은 오브젝트의 Rigidbody), R 로 다른 오브젝트의 Rotator 를 켜고 끈다.
// 유니티의 MonoBehaviour 처럼 Update 안에서 입력을 읽고 Transform 을 직접 조작한다. 자기 스크립트를 쓸 때 이 파일 쌍을 복사해 시작하면 된다.
// 방향키를 쓰는 이유: WASD 는 에디터 카메라가 쓴다 (TestApp::UpdateCamera). 게임 카메라가 활성이면 에디터 카메라 이동은 꺼진다.
class PlayerController : public Script
{
public:
	const char* GetTypeName() const override;   // SHERLOCK_SCRIPT 가 정의한다 (.cpp)
	void Reflect(PropertyVisitor& v) override;
	void Start() override;
	void Update(float dt) override;

	float speed = 6.0f;
	float boost = 3.0f;
	float jumpSpeed = 7.0f;
	std::string rotatorObject = "Cylinder";   // R 키로 토글할 Rotator 가 붙은 오브젝트 이름

private:
	Rotator* m_rotator = nullptr;   // Start 에서 찾는다. Stop → 재생마다 다시 찾는다 (스냅샷 복원으로 오브젝트가 새로 만들어진다)
};

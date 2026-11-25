#pragma once
#include "Scene/Script.h"
#include "Core/LookSmoother.h"

// 자유시점 카메라. 게임 카메라(CameraComponent)가 붙은 오브젝트에 붙이면 에디터 카메라처럼 날아다닐 수 있음.
//   마우스 오른쪽 버튼을 누른 채 움직이면 시선 회전, W/S/A/D 앞뒤·좌우, E/Space 위, Q/Ctrl 아래,
//   Shift 로 가속, 휠로 이동 속도 조절. 빌드한 게임(SherlockGame)에서도 같은 입력으로 동작함 (ESC 로 종료).
class FreeCamera : public Script
{
public:
	const char* GetTypeName() const override;   // SHERLOCK_SCRIPT 가 정의함 (.cpp)
	void Reflect(PropertyVisitor& v) override;
	void Start() override;
	void Update(float dt) override;

	float speed = 4.0f;          // 초당 이동 거리
	float boost = 3.0f;          // Shift 를 누르면 곱해지는 배율
	float lookSpeed = 0.0025f;   // 마우스 1 픽셀당 회전 (라디안)

private:
	float m_yaw = 0.0f;
	float m_pitch = 0.0f;
	bool m_lookBlocked = false;   // 오른쪽 버튼을 게임 UI 위에서 눌렀음 (뗄 때까지 시선 회전 안 함)
	LookSmoother m_look;   // 마우스 회전을 시간에 고르게 나눔 (높은 fps 에서 모션 블러가 번쩍이는 것을 막음)
};

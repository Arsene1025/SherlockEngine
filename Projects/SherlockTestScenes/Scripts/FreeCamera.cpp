#include "pch.h"
#include "FreeCamera.h"
#include <algorithm>
#include <cmath>

void FreeCamera::Reflect(PropertyVisitor& v)
{
	v.Float("speed", speed, 0.1f);
	v.Float("boost", boost, 0.1f);
	v.Float("lookSpeed", lookSpeed, 0.0001f);
}

void FreeCamera::Start()
{
	// 씬에 놓인 카메라의 방향에서 시작함
	m_pitch = GetRotation().x;
	m_yaw = GetRotation().y;
}

void FreeCamera::Update(float dt)
{
	Input& input = GetInput();

	// 시선 회전: 오른쪽 버튼을 누르고 있는 동안만. 아래로 움직이면 아래를 봄 (pitch 가 양수 = 아래).
	if (input.IsMouseDown(MouseButton::Right))
	{
		constexpr float kMaxPitch = 1.55f;   // 약 89 도. 수직에서 뒤집히지 않게 함
		m_yaw += static_cast<float>(input.GetMouseDeltaX()) * lookSpeed;
		m_pitch = std::clamp(m_pitch + static_cast<float>(input.GetMouseDeltaY()) * lookSpeed, -kMaxPitch, kMaxPitch);
		GetTransform().SetRotation(m_pitch, m_yaw, 0.0f);
	}

	// 휠로 이동 속도를 조절함 (한 칸에 10%)
	const float wheel = input.GetWheelDelta();
	if (wheel != 0.0f) speed = std::clamp(speed * std::pow(1.1f, wheel), 0.1f, 200.0f);

	float forward = 0.0f, right = 0.0f, up = 0.0f;
	if (input.IsKeyDown('W')) forward += 1.0f;
	if (input.IsKeyDown('S')) forward -= 1.0f;
	if (input.IsKeyDown('D')) right += 1.0f;
	if (input.IsKeyDown('A')) right -= 1.0f;
	if (input.IsKeyDown('E') || input.IsKeyDown(VK_SPACE)) up += 1.0f;
	if (input.IsKeyDown('Q') || input.IsKeyDown(VK_CONTROL)) up -= 1.0f;

	const float length = std::sqrt(forward * forward + right * right + up * up);
	if (length == 0.0f) return;
	const float velocity = speed * (input.IsKeyDown(VK_SHIFT) ? boost : 1.0f) * dt / length;   // 대각선도 같은 속도
	TranslateLocal(forward * velocity, right * velocity, 0.0f);
	Translate(0.0f, up * velocity, 0.0f);   // 위아래는 시선과 무관하게 월드 Y
}

SHERLOCK_SCRIPT(FreeCamera)

#include "pch.h"
#include "FreeCamera.h"
#include "UI/UISystem.h"
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
	m_look.Reset();
}

void FreeCamera::Update(float dt)
{
	Input& input = GetInput();
	// 게임 UI 위에서 시작한 누름·휠은 UI 것 (유니티 EventSystem.IsPointerOverGameObject 와 같은 쓰임)
	const UISystem* ui = GetUI();
	const bool overUI = ui != nullptr && (ui->IsPointerOverUI() || ui->IsPointerCaptured());
	if (input.IsMousePressed(MouseButton::Right))
	{
		m_lookBlocked = overUI;
	}

	// 시선 회전: 오른쪽 버튼을 누르고 있는 동안만. 아래로 움직이면 아래를 봄 (pitch 가 양수 = 아래).
	// 받은 회전량은 쌓아 두고 시간에 고르게 나눠 적용함 (LookSmoother). 버튼을 뗀 뒤에도 남은 몫을 마저 냄.
	if (input.IsMouseDown(MouseButton::Right) && !m_lookBlocked)
	{
		m_look.Add(static_cast<float>(input.GetMouseDeltaX()) * lookSpeed, static_cast<float>(input.GetMouseDeltaY()) * lookSpeed);
	}
	if (m_look.HasPending())
	{
		constexpr float kMaxPitch = 1.55f;   // 약 89 도. 수직에서 뒤집히지 않게 함
		float yaw = 0.0f, pitch = 0.0f;
		m_look.Consume(dt, yaw, pitch);
		m_yaw += yaw;
		m_pitch = std::clamp(m_pitch + pitch, -kMaxPitch, kMaxPitch);
		GetTransform().SetRotation(m_pitch, m_yaw, 0.0f);
	}

	// 휠로 이동 속도를 조절함 (한 칸에 10%)
	const float wheel = input.GetWheelDelta();
	if (wheel != 0.0f && !overUI)
	{
		speed = std::clamp(speed * std::pow(1.1f, wheel), 0.1f, 200.0f);
	}

	float forward = 0.0f, right = 0.0f, up = 0.0f;
	if (input.IsKeyDown('W'))
	{
		forward += 1.0f;
	}
	if (input.IsKeyDown('S'))
	{
		forward -= 1.0f;
	}
	if (input.IsKeyDown('D'))
	{
		right += 1.0f;
	}
	if (input.IsKeyDown('A'))
	{
		right -= 1.0f;
	}
	if (input.IsKeyDown('E') || input.IsKeyDown(VK_SPACE))
	{
		up += 1.0f;
	}
	if (input.IsKeyDown('Q') || input.IsKeyDown(VK_CONTROL))
	{
		up -= 1.0f;
	}

	const float length = std::sqrt(forward * forward + right * right + up * up);
	if (length == 0.0f) return;
	const float velocity = speed * (input.IsKeyDown(VK_SHIFT) ? boost : 1.0f) * dt / length;   // 대각선도 같은 속도
	TranslateLocal(forward * velocity, right * velocity, 0.0f);
	Translate(0.0f, up * velocity, 0.0f);   // 위아래는 시선과 무관하게 월드 Y
}

SHERLOCK_SCRIPT(FreeCamera)

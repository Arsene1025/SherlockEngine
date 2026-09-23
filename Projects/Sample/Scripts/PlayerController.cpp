#include "pch.h"
#include "PlayerController.h"
#include "Rotator.h"           // 다른 스크립트의 타입 (GetBehaviour<Rotator>)
#include "Game/Components/Rigidbody.h"      // 엔진 컴포넌트의 타입 (GetComponent<Rigidbody>)
#include "Core/Log.h"

void PlayerController::Reflect(PropertyVisitor& v)
{
	v.Float("speed", speed);
	v.Float("boost", boost);
	v.Float("jumpSpeed", jumpSpeed);
	v.String("rotatorObject", rotatorObject);
}

void PlayerController::Start()
{
	// 다른 오브젝트의 스크립트 참조: 이름으로 오브젝트를 찾고 타입으로 컴포넌트를 찾는다.
	m_rotator = nullptr;
	if (GameObject* target = Find(rotatorObject)) m_rotator = target->GetBehaviour<Rotator>();
	if (m_rotator == nullptr) Log::Warn("PlayerController: '%s' 에 Rotator 가 없어 R 키는 아무것도 하지 않는다.", rotatorObject.c_str());
}

void PlayerController::Update(float dt)
{
	Input& input = GetInput();
	float dx = 0.0f, dz = 0.0f;
	if (input.IsKeyDown(VK_LEFT)) dx -= 1.0f;
	if (input.IsKeyDown(VK_RIGHT)) dx += 1.0f;
	if (input.IsKeyDown(VK_UP)) dz += 1.0f;
	if (input.IsKeyDown(VK_DOWN)) dz -= 1.0f;
	if (dx != 0.0f || dz != 0.0f)
	{
		float velocity = speed * (input.IsKeyDown(VK_SHIFT) ? boost : 1.0f);
		if (dx != 0.0f && dz != 0.0f) velocity *= 0.7071f;   // 대각선 정규화
		Translate(dx * velocity * dt, 0.0f, dz * velocity * dt);
	}

	// 점프: 같은 오브젝트의 Rigidbody 가 있고 바닥에 닿아 있을 때만
	if (input.IsKeyPressed(VK_SPACE))
	{
		if (Rigidbody* body = GetComponent<Rigidbody>())
		{
			if (body->IsGrounded()) body->AddImpulse(0.0f, jumpSpeed, 0.0f);
		}
	}

	// 다른 스크립트 호출: R 키로 Rotator 를 켜고 끈다
	if (input.IsKeyPressed('R') && m_rotator != nullptr) m_rotator->Toggle();
}

SHERLOCK_SCRIPT(PlayerController)

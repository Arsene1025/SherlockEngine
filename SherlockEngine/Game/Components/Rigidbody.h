#pragma once
#include "Scene/Behaviour.h"

// 간단한 강체 (엔진 컴포넌트): 중력 + 속도 적분 + y = groundY 바닥과의 반발. 물리 엔진 대신 쓰는 임시 구현임 — 다른 오브젝트와는 충돌하지 않음.
// FixedUpdate 에서 실행됨 (10단계 고정 스텝): 프레임 속도와 무관하게 같은 궤적을 그림.
// 스크립트에서 GetComponent<Rigidbody>()->AddImpulse(0, 6, 0) 처럼 씀 (헤더를 따로 둔 이유).
class Rigidbody : public Behaviour
{
public:
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;
	void Start() override;
	void FixedUpdate(float dt) override;

	void AddImpulse(float x, float y, float z) { m_velocity.x += x; m_velocity.y += y; m_velocity.z += z; }   // 즉시 속도 변화 (질량 1)
	void SetVelocity(const DirectX::XMFLOAT3& v) { m_velocity = v; }
	const DirectX::XMFLOAT3& GetVelocity() const { return m_velocity; }
	bool IsGrounded() const { return m_grounded; }

	DirectX::XMFLOAT3 initialVelocity = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	float gravity = -9.8f;
	float bounciness = 0.6f;
	float radius = 1.5f;     // 바닥에 닿는 아래쪽 반경 (구 반지름 / 큐브 절반)
	float groundY = 0.0f;
	float drag = 0.1f;

private:
	DirectX::XMFLOAT3 m_velocity = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	bool m_grounded = false;
};

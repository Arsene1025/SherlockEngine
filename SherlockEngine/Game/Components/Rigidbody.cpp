#include "pch.h"
#include "Game/Components/Rigidbody.h"
#include "Scene/GameObject.h"

void Rigidbody::Reflect(PropertyVisitor& v)
{
	v.Float3("initialVelocity", initialVelocity);
	v.Float("gravity", gravity);
	v.Float("bounciness", bounciness, 0.01f);
	v.Float("radius", radius);
	v.Float("groundY", groundY);
	v.Float("drag", drag, 0.01f);
}

void Rigidbody::Start()
{
	m_velocity = initialVelocity;
	m_grounded = false;
}

void Rigidbody::FixedUpdate(float dt)
{
	m_velocity.y += gravity * dt;
	m_velocity.x *= 1.0f - drag * dt;
	m_velocity.z *= 1.0f - drag * dt;
	DirectX::XMFLOAT3 p = GetTransform().GetPosition();
	p.x += m_velocity.x * dt;
	p.y += m_velocity.y * dt;
	p.z += m_velocity.z * dt;
	m_grounded = false;
	if (p.y - radius <= groundY)
	{
		p.y = groundY + radius;
		if (m_velocity.y < 0.0f) m_velocity.y = -m_velocity.y * bounciness;
		if (m_velocity.y < 0.05f) { m_velocity.y = 0.0f; m_grounded = true; }   // 미세한 되튐은 멈추고 바닥에 닿은 것으로 처리
	}
	GetTransform().SetPosition(p);
}

SHERLOCK_BEHAVIOUR(Rigidbody)

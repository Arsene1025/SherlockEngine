#include "pch.h"
#include "Rotator.h"

// 공전·진동·맥동 같은 움직임도 같은 식으로 쓴다: SetPosition(center + (r cos t, h, r sin t)), SetScale(1 + A sin(ft)) …

void Rotator::Reflect(PropertyVisitor& v)
{
	v.Float3("degreesPerSecond", degreesPerSecond, 1.0f);
}

void Rotator::Update(float dt)
{
	Rotate(DirectX::XMConvertToRadians(degreesPerSecond.x) * dt,
		DirectX::XMConvertToRadians(degreesPerSecond.y) * dt,
		DirectX::XMConvertToRadians(degreesPerSecond.z) * dt);
}

SHERLOCK_SCRIPT(Rotator)

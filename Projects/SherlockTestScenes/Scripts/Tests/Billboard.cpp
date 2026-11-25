#include "pch.h"
#include "Billboard.h"
#include <cmath>
#include "Scene/Camera.h"

void Billboard::Reflect(PropertyVisitor& v)
{
	v.Bool("yawOnly", yawOnly);
}

void Billboard::Update(float dt)
{
	(void)dt;
	const Camera* camera = GetContext().camera;
	if (camera == nullptr) return;
	// 왼손 좌표: 캔버스의 +z 가 카메라에서 멀어지는 쪽이면 글자가 바로 보임 → 오브젝트 forward = (오브젝트 − 카메라)
	const DirectX::XMFLOAT3 p = GetWorldPosition();
	const DirectX::XMFLOAT3& c = camera->GetPosition();
	const float dx = p.x - c.x, dy = p.y - c.y, dz = p.z - c.z;
	const float yaw = std::atan2(dx, dz);
	const float pitch = yawOnly ? 0.0f : std::atan2(-dy, std::sqrt(dx * dx + dz * dz));
	GetTransform().SetRotation(pitch, yaw, 0.0f);
}

SHERLOCK_SCRIPT(Billboard)

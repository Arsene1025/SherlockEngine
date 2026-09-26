#include "pch.h"
#include "Scene/Behaviour.h"
#include "Scene/GameObject.h"
#include "Scene/Scene.h"
#include <algorithm>
#include <cmath>

// 추적: 소유 오브젝트를 이름이 target 인 오브젝트의 offset 위치로 부드럽게 옮기고, target(+lookOffset) 을 바라보게 회전함.
// 카메라 오브젝트(CameraComponent)에 붙이면 3인칭 추적 카메라가 됨 — 카메라 자체는 CameraComponent 가 맡고
// 이 컴포넌트는 "카메라 오브젝트를 옮기는 로직" 일 뿐임. 그래서 어떤 오브젝트에 붙여도 대상을 따라다니게 할 수 있음.
// Start 에서 순간이동하지 않음: 에디터에서 놓아 둔 자리에서 출발해 smoothing 속도로 따라붙음.
class FollowTarget : public Behaviour
{
public:
	const char* GetTypeName() const override;

	void Reflect(PropertyVisitor& v) override
	{
		v.String("target", target);
		v.Float3("offset", offset);
		v.Float3("lookOffset", lookOffset);
		v.Float("smoothing", smoothing, 0.1f);
	}

	void Start() override { m_target = nullptr; }

	void Update(float dt) override
	{
		if (m_target == nullptr) m_target = GetContext().scene->FindObject(target);   // 재생 중 생긴 오브젝트도 찾음
		if (m_target == nullptr || m_target == &GetOwner()) return;

		const DirectX::XMFLOAT3 tp = m_target->GetTransform().GetPosition();
		const DirectX::XMFLOAT3 desired(tp.x + offset.x, tp.y + offset.y, tp.z + offset.z);
		DirectX::XMFLOAT3 p = GetTransform().GetPosition();
		const float k = smoothing <= 0.0f ? 1.0f : (std::min)(1.0f, dt * smoothing);
		p.x += (desired.x - p.x) * k;
		p.y += (desired.y - p.y) * k;
		p.z += (desired.z - p.z) * k;
		GetTransform().SetPosition(p);

		// target 을 바라보는 yaw/pitch (Camera::SetLookAt 과 같은 식: forward = (cos p·sin y, −sin p, cos p·cos y))
		const float dx = tp.x + lookOffset.x - p.x;
		const float dy = tp.y + lookOffset.y - p.y;
		const float dz = tp.z + lookOffset.z - p.z;
		const float length = sqrtf(dx * dx + dy * dy + dz * dz);
		if (length < 1e-4f) return;
		GetTransform().SetRotation(-asinf(dy / length), atan2f(dx, dz), 0.0f);
	}

	std::string target = "SphereCenter";
	DirectX::XMFLOAT3 offset = DirectX::XMFLOAT3(0.0f, 6.0f, -12.0f);
	DirectX::XMFLOAT3 lookOffset = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
	float smoothing = 4.0f;

private:
	GameObject* m_target = nullptr;
};
SHERLOCK_BEHAVIOUR(FollowTarget)

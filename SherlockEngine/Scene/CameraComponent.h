#pragma once
#include "Scene/Behaviour.h"

class Camera;

// 11-C단계: 카메라 컴포넌트. 이것이 붙은 오브젝트가 "씬 안의 카메라" 다 (유니티의 Main Camera, 언리얼의 CameraActor).
//
// 자세는 소유 오브젝트의 Transform 이다: position 이 눈, rotation.x 가 pitch, rotation.y 가 yaw (Camera 와 같은
// RollPitchYaw 규약). 에디터에서 기즈모로 옮기고 씬 파일에 저장된다. 편집 중에는 에디터 카메라로 보고, 재생 중에는
// Scene::ApplyActiveCamera 가 매 프레임 활성 카메라의 자세를 엔진 Camera 에 쓴다 (Stop 이 에디터 시점을 되돌린다).
//
// 카메라가 여럿이면 priority 가 가장 높은 enabled 카메라가 활성이다. 재생 중 priority 를 바꾸거나 enabled 를 켜고 끄면
// 카메라가 전환되고, 새 카메라의 blendTime 동안 이전 시점에서 보간한다 — 컷씬·시점 전환의 기초.
struct CameraPose
{
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	float yaw = 0.0f;
	float pitch = 0.0f;
	float fovY = 0.0f;   // 라디안
};

class CameraComponent : public Behaviour
{
public:
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& visitor) override;

	CameraPose GetPose() const;
	// 위치·yaw/pitch 를 (applyLens 면 fov·near/far 도) 카메라에 쓴다. 종횡비는 씬 뷰가 정하므로 건드리지 않는다.
	void ApplyTo(Camera& camera, bool applyLens = true) const;
	// 반대 방향: 카메라의 자세와 렌즈를 이 오브젝트로 (에디터의 "Align to view").
	void SetFromCamera(const Camera& camera);

	float fovYDegrees = 45.0f;
	float nearZ = 0.1f;
	float farZ = 1000.0f;
	int priority = 0;        // 높은 것이 활성. 같으면 오브젝트 순서
	float blendTime = 0.5f;  // 재생 "도중" 다른 카메라에서 이 카메라로 전환될 때의 보간 시간 (초). 0 = 즉시.
	                         // Play 시작은 언제나 즉시 — 에디터 시점에서 카메라로 미끄러지지 않는다 (Scene::ApplyActiveCamera).
};

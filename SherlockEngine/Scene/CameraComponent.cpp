#include "pch.h"
#include "Scene/CameraComponent.h"
#include "Scene/GameObject.h"
#include "Scene/Camera.h"

using namespace DirectX;

void CameraComponent::Reflect(PropertyVisitor& v)
{
	v.Float("fovY", fovYDegrees, 0.5f);
	v.Float("nearZ", nearZ, 0.01f);
	v.Float("farZ", farZ, 1.0f);
	v.Int("priority", priority);
	v.Float("blendTime", blendTime, 0.05f);
}

CameraPose CameraComponent::GetPose() const
{
	CameraPose pose;
	pose.fovY = XMConvertToRadians(fovYDegrees);

	// 월드 행렬 → 스케일 / 회전(쿼터니언) / 이동. 부모의 회전이 여기서 합쳐져 들어옴.
	const GameObject& owner = GetOwner();
	XMVECTOR scale, rotation, translation;
	if (XMMatrixDecompose(&scale, &rotation, &translation, owner.GetWorldMatrix()))
	{
		XMStoreFloat3(&pose.position, translation);
		XMStoreFloat4(&pose.rotation, XMQuaternionNormalize(rotation));
	}
	else
	{
		// 분해 실패(부모 스케일 0 등): 로컬 값으로 대체함. 화면이 사라지는 것보다 낫다.
		const Transform& t = owner.GetTransform();
		pose.position = t.GetPosition();
		XMStoreFloat4(&pose.rotation, XMQuaternionRotationMatrix(t.GetRotationMatrix()));
	}
	return pose;
}

void CameraComponent::ApplyTo(Camera& camera, bool applyLens) const
{
	const CameraPose pose = GetPose();
	camera.SetPosition(pose.position);
	camera.SetRotation(pose.GetRotationMatrix());
	if (applyLens) camera.SetLens(pose.fovY, camera.GetAspect(), nearZ, farZ);
}

void CameraComponent::SetFromCamera(const Camera& camera)
{
	// 카메라의 월드 자세(R·T, 스케일 1)를 오브젝트의 월드 자세로. 부모가 있으면 GameObject 가 로컬로 변환함.
	GameObject& owner = GetOwner();
	const XMFLOAT3& p = camera.GetPosition();
	owner.SetWorldMatrix(camera.GetRotationMatrix() * XMMatrixTranslation(p.x, p.y, p.z));
	fovYDegrees = XMConvertToDegrees(camera.GetFovY());
	nearZ = camera.GetNearZ();
	farZ = camera.GetFarZ();
}

SHERLOCK_BEHAVIOUR(CameraComponent)

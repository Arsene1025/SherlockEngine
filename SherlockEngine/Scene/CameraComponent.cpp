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
	const Transform& t = GetOwner().GetTransform();
	CameraPose pose;
	pose.position = t.GetPosition();
	pose.pitch = t.GetRotation().x;
	pose.yaw = t.GetRotation().y;
	pose.fovY = XMConvertToRadians(fovYDegrees);
	return pose;
}

void CameraComponent::ApplyTo(Camera& camera, bool applyLens) const
{
	const CameraPose pose = GetPose();
	camera.SetPosition(pose.position);
	camera.SetYawPitch(pose.yaw, pose.pitch);
	if (applyLens) camera.SetLens(pose.fovY, camera.GetAspect(), nearZ, farZ);
}

void CameraComponent::SetFromCamera(const Camera& camera)
{
	Transform& t = GetOwner().GetTransform();
	t.SetPosition(camera.GetPosition());
	t.SetRotation(camera.GetPitch(), camera.GetYaw(), 0.0f);
	fovYDegrees = XMConvertToDegrees(camera.GetFovY());
	nearZ = camera.GetNearZ();
	farZ = camera.GetFarZ();
}

SHERLOCK_BEHAVIOUR(CameraComponent)

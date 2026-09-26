#include "pch.h"
#include "Scene/Transform.h"
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

Transform::Transform()
	: position(0.0f, 0.0f, 0.0f),
	  rotation(0.0f, 0.0f, 0.0f),
	  scale(1.0f, 1.0f, 1.0f)
{
	XMStoreFloat4x4(&preTransform, XMMatrixIdentity());
}

void Transform::SetPosition(const XMFLOAT3& value)
{
	position = value;
}

void Transform::SetPosition(float x, float y, float z)
{
	position = XMFLOAT3(x, y, z);
}

void Transform::SetRotation(const XMFLOAT3& value)
{
	rotation = value;
}

void Transform::SetRotation(float x, float y, float z)
{
	rotation = XMFLOAT3(x, y, z);
}

void Transform::SetScale(const XMFLOAT3& value)
{
	scale = value;
}

void Transform::SetScale(float x, float y, float z)
{
	scale = XMFLOAT3(x, y, z);
}

void Transform::SetPreTransform(const XMMATRIX& value)
{
	XMStoreFloat4x4(&preTransform, value);
	hasPreTransform = true;
}

void Transform::Translate(float x, float y, float z)
{
	position.x += x;
	position.y += y;
	position.z += z;
}

void Transform::Translate(const XMFLOAT3& delta)
{
	Translate(delta.x, delta.y, delta.z);
}

void Transform::TranslateLocal(float forward, float right, float up)
{
	XMVECTOR p = XMLoadFloat3(&position);
	p += GetForward() * forward + GetRight() * right + GetUp() * up;
	XMStoreFloat3(&position, p);
}

void Transform::Rotate(float pitch, float yaw, float roll)
{
	rotation.x += pitch;
	rotation.y += yaw;
	rotation.z += roll;
}

void Transform::LookAt(const XMFLOAT3& target)
{
	// forward = (cos p·sin y, −sin p, cos p·cos y) 를 거꾸로 풀어 회전을 구함 (Camera::SetLookAt 과 같음)
	const float dx = target.x - position.x;
	const float dy = target.y - position.y;
	const float dz = target.z - position.z;
	const float length = sqrtf(dx * dx + dy * dy + dz * dz);
	if (length < 1e-6f) return;
	rotation = XMFLOAT3(-asinf(dy / length), atan2f(dx, dz), 0.0f);
}

XMVECTOR Transform::GetForward() const
{
	return XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z));
}

XMVECTOR Transform::GetRight() const
{
	return XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z));
}

XMVECTOR Transform::GetUp() const
{
	return XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z));
}

XMMATRIX Transform::GetWorldMatrix() const
{
	XMMATRIX scaleMatrix = XMMatrixScaling(scale.x, scale.y, scale.z);
	XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
	XMMATRIX translationMatrix = XMMatrixTranslation(position.x, position.y, position.z);

	XMMATRIX world = scaleMatrix * rotationMatrix * translationMatrix;
	if (hasPreTransform)
	{
		world = XMLoadFloat4x4(&preTransform) * world;
	}
	return world;
}

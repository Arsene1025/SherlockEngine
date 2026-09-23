#include "pch.h"
#include "Scene/Transform.h"

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

#include "pch.h"
#include "Transform.h"

Transform::Transform()
	: position(0.0f, 0.0f, 0.0f),
	  rotation(0.0f, 0.0f, 0.0f),
	  scale(1.0f, 1.0f, 1.0f)
{
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

XMMATRIX Transform::GetWorldMatrix() const
{
	XMMATRIX scaleMatrix = XMMatrixScaling(scale.x, scale.y, scale.z);
	XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
	XMMATRIX translationMatrix = XMMatrixTranslation(position.x, position.y, position.z);

	return scaleMatrix * rotationMatrix * translationMatrix;
}

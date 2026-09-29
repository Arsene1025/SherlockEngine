#include "pch.h"
#include "Scene/Transform.h"
#include <algorithm>
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

XMMATRIX Transform::GetRotationMatrix() const
{
	return XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
}

XMVECTOR Transform::GetForward() const
{
	return XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), GetRotationMatrix());
}

XMVECTOR Transform::GetRight() const
{
	return XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), GetRotationMatrix());
}

XMVECTOR Transform::GetUp() const
{
	return XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), GetRotationMatrix());
}

XMMATRIX Transform::GetLocalMatrix() const
{
	XMMATRIX scaleMatrix = XMMatrixScaling(scale.x, scale.y, scale.z);
	XMMATRIX rotationMatrix = GetRotationMatrix();
	XMMATRIX translationMatrix = XMMatrixTranslation(position.x, position.y, position.z);

	XMMATRIX local = scaleMatrix * rotationMatrix * translationMatrix;
	if (hasPreTransform)
	{
		local = XMLoadFloat4x4(&preTransform) * local;
	}
	return local;
}

bool Transform::SetLocalMatrix(const XMMATRIX& localWithPre)
{
	// local = pre × S·R·T 이므로 S·R·T = pre⁻¹ × local.
	XMMATRIX srt = localWithPre;
	if (hasPreTransform) srt = XMMatrixInverse(nullptr, XMLoadFloat4x4(&preTransform)) * srt;
	XMVECTOR s, q, t;
	if (!XMMatrixDecompose(&s, &q, &t, srt)) return false;   // 스케일 0 이거나 행렬이 퇴화하면 실패
	XMFLOAT4X4 rot;
	XMStoreFloat4x4(&rot, XMMatrixRotationQuaternion(q));
	XMStoreFloat3(&scale, s);
	XMStoreFloat3(&position, t);
	rotation = EulerFromRotationMatrix(rot);
	return true;
}

XMFLOAT3 Transform::EulerFromRotationMatrix(const XMFLOAT4X4& m)
{
	// R = Rz(roll)·Rx(pitch)·Ry(yaw) (행벡터). 세 번째 행이 forward = (cos p·sin y, −sin p, cos p·cos y) 이므로
	// m[2][1] = −sin(pitch) 에서 pitch 를, m[2][0]/m[2][2] 에서 yaw 를, 첫 열 두 성분에서 roll 을 얻음.
	XMFLOAT3 euler;
	const float sp = -m.m[2][1];
	euler.x = asinf(std::clamp(sp, -1.0f, 1.0f));
	if (fabsf(sp) < 0.9999f)
	{
		euler.y = atan2f(m.m[2][0], m.m[2][2]);
		euler.z = atan2f(m.m[0][1], m.m[1][1]);
	}
	else
	{
		// 짐벌락: yaw 축과 roll 축이 겹쳐 둘을 구분할 수 없음. yaw 를 0 으로 두고 roll 에 몰아줌.
		euler.y = 0.0f;
		euler.z = atan2f(-m.m[1][0], m.m[0][0]);
	}
	return euler;
}

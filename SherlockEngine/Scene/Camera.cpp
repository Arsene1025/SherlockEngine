#include "pch.h"
#include "Scene/Camera.h"
#include <cmath>
#include <cassert>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	// 90도보다 살짝 작게 제한함. 정확히 90도면 forward가 up과 나란해져 right가 정의되지 않음.
	constexpr float kPitchLimit = XM_PIDIV2 - 0.01f;

	float WrapAngle(float angle)
	{
		// [−π, π] 범위로 되돌림. 오래 돌려도 값이 계속 커지지 않아 누적 오차가 생기지 않음.
		while (angle > XM_PI) angle -= XM_2PI;
		while (angle < -XM_PI) angle += XM_2PI;
		return angle;
	}

	float ClampPitch(float pitch)
	{
		if (pitch > kPitchLimit) return kPitchLimit;
		if (pitch < -kPitchLimit) return -kPitchLimit;
		return pitch;
	}
}

Camera::Camera()
	: m_position(0.0f, 0.0f, 0.0f),
	  m_yaw(0.0f),
	  m_pitch(0.0f),
	  m_fovY(XM_PIDIV4),
	  m_aspect(16.0f / 9.0f),
	  m_nearZ(0.1f),
	  m_farZ(1000.0f),
	  m_orthoHeight(25.0f),
	  m_usePerspective(true)
{
	XMStoreFloat4x4(&m_view, XMMatrixIdentity());
	XMStoreFloat4x4(&m_proj, XMMatrixIdentity());

	// 이전 LookAt 카메라의 기본값과 같은 시점에서 시작함. 2단계의 첫 프레임이 1단계와 같아야 하기 때문.
	SetLookAt(XMFLOAT3(10.0f, 20.0f, -45.0f), XMFLOAT3(0.0f, 5.0f, 0.0f));
	UpdateProjectionMatrix();
}

void Camera::SetPosition(const XMFLOAT3& position)
{
	m_position = position;
	UpdateViewMatrix();
}

void Camera::SetYawPitch(float yaw, float pitch)
{
	m_yaw = WrapAngle(yaw);
	m_pitch = ClampPitch(pitch);
	UpdateViewMatrix();
}

void Camera::Rotate(float deltaYaw, float deltaPitch)
{
	SetYawPitch(m_yaw + deltaYaw, m_pitch + deltaPitch);
}

void Camera::Move(float forward, float right, float up)
{
	XMVECTOR position = XMLoadFloat3(&m_position);
	position += GetForward() * forward;
	position += GetRight() * right;
	position += XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f) * up;   // 상하 이동은 월드 Y 기준. 비행 카메라의 관례.
	XMStoreFloat3(&m_position, position);
	UpdateViewMatrix();
}

void Camera::SetLookAt(const XMFLOAT3& eye, const XMFLOAT3& target)
{
	const XMVECTOR direction = XMVector3Normalize(XMLoadFloat3(&target) - XMLoadFloat3(&eye));
	const float dx = XMVectorGetX(direction);
	const float dy = XMVectorGetY(direction);
	const float dz = XMVectorGetZ(direction);

	// forward = (cos p·sin y, −sin p, cos p·cos y) 를 거꾸로 풀어 yaw/pitch 를 구함.
	m_position = eye;
	m_yaw = WrapAngle(atan2f(dx, dz));
	m_pitch = ClampPitch(-asinf(dy));
	UpdateViewMatrix();
}

XMMATRIX Camera::GetRotationMatrix() const
{
	// 인자 순서는 (pitch, yaw, roll). Transform::GetWorldMatrix와 같은 함수를 씀.
	return XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0.0f);
}

XMVECTOR Camera::GetForward() const
{
	return XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), GetRotationMatrix());
}

XMVECTOR Camera::GetRight() const
{
	return XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), GetRotationMatrix());
}

XMVECTOR Camera::GetUp() const
{
	return XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), GetRotationMatrix());
}

void Camera::SetLens(float fovY, float aspect, float zn, float zf)
{
	m_fovY = fovY;
	m_aspect = aspect;
	m_nearZ = zn;
	m_farZ = zf;
	UpdateProjectionMatrix();
}

void Camera::SetAspectRatio(float aspect)
{
	m_aspect = aspect;
	UpdateProjectionMatrix();
}

void Camera::SetUsePerspectiveProjection(bool usePerspective)
{
	m_usePerspective = usePerspective;
	UpdateProjectionMatrix();
}

XMMATRIX Camera::GetViewMatrix() const
{
	return XMLoadFloat4x4(&m_view);
}

XMMATRIX Camera::GetProjectionMatrix() const
{
	return XMLoadFloat4x4(&m_proj);
}

XMMATRIX Camera::GetViewProjectionMatrix() const
{
	return GetViewMatrix() * GetProjectionMatrix();
}

void Camera::UpdateViewMatrix()
{
	// 카메라의 월드 변환 = R · T. 뷰행렬은 그 역행렬임.
	// 회전 행렬은 직교이므로 역행렬이 전치이고, 평행이동의 역은 부호 반전이라
	// 일반 역행렬을 구할 필요는 없지만, "뷰 = 월드의 역"이라는 정의를 코드에
	// 그대로 남기기 위해 XMMatrixInverse를 씀. 프레임당 한 번이라 비용은 무시할 만함.
	const XMMATRIX world = GetRotationMatrix() * XMMatrixTranslation(m_position.x, m_position.y, m_position.z);
	const XMMATRIX view = XMMatrixInverse(nullptr, world);

#if defined(_DEBUG)
	// 같은 결과를 내야 하는 전용 함수(XMMatrixLookToLH)와 대조함. 기저 벡터의 정의가 어긋나면 여기서 잡힘.
	const XMMATRIX lookTo = XMMatrixLookToLH(XMLoadFloat3(&m_position), GetForward(), GetUp());
	for (int row = 0; row < 4; ++row)
	{
		const XMVECTOR diff = XMVectorAbs(view.r[row] - lookTo.r[row]);
		assert(XMVector4LessOrEqual(diff, XMVectorReplicate(1e-3f)) && "Camera: inverse(R*T) != LookToLH");
	}
#endif

	XMStoreFloat4x4(&m_view, view);
}

void Camera::UpdateProjectionMatrix()
{
	float aspect = m_aspect;
	if (aspect <= 0.0f)
	{
		aspect = 1.0f;
	}

	XMMATRIX proj = XMMatrixIdentity();
	if (m_usePerspective)
	{
		proj = XMMatrixPerspectiveFovLH(m_fovY, aspect, m_nearZ, m_farZ);
	}
	else
	{
		proj = XMMatrixOrthographicLH(m_orthoHeight * aspect, m_orthoHeight, m_nearZ, m_farZ);
	}

	XMStoreFloat4x4(&m_proj, proj);
}

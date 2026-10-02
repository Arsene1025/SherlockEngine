#include "pch.h"
#include "Scene/Camera.h"
#include <algorithm>
#include <cmath>
#include <cassert>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	// 90도보다 살짝 작게 제한함. 정확히 90도면 forward가 up과 나란해져 right가 정의되지 않음.
	constexpr float kPitchLimit = XM_PIDIV2 - 0.01f;

	/*
	카메라를 Yaw로 돌릴 때 360도로 계속 돌릴 수 있어야 하는데 각도를 그대로 두면 Yaw의 값이 끝도 없이 커지거나 작아진다
	따라서 각도를 360도 더하거나 빼서 각도가 일정한 범위 안에 존재하도록 만드는 것
	정확히 360도를 빼기 때문에 카메라가 회전할 일은 없음
	while을 사용함으로서 입력이 얼마가 되든 [−π, π]로 돌아오는걸 보장.
	*/
	float WrapAngle(float angle)
	{
		// [−π, π] 범위로 되돌림. 오래 돌려도 값이 계속 커지지 않아 값이 커짐으로서 생기는 정밀도 손실을 막음.
		// float는 유효 숫자의 개수가 고정되어있음, 값이 커지면 자릿수를 정수 쪽에서 사용하고 소수 쪽에 남는 자릿수가 줄어듬.
		// 소수 자리수가 줄어들면 필연적으로 정밀도가 손실됨.
		while (angle > XM_PI) angle -= XM_2PI;
		while (angle < -XM_PI) angle += XM_2PI;
		return angle;
	}
	/*
	Pitch를 클램프로 처리하는 이유 -> 카메라를 올렸을 때 90도가 되면 월드의 UP벡터와 카메라의 Foward벡터가 일치함.
	그렇게 되면 LookAt계산의 외적이 0이 되어버리고 90도가 넘으면 화면이 뒤집힘.
	이를 막기위해 90도보다 0.01 rad(약 0.57도) 작은 약 89.43도로 Pitch를 Clamp함 (kPitchLimit).
	*/
	float ClampPitch(float pitch)
	{
		if (pitch > kPitchLimit) return kPitchLimit;
		if (pitch < -kPitchLimit) return -kPitchLimit;
		return pitch;
	}
}

Camera::Camera()
	: m_position(0.0f, 0.0f, 0.0f),
	  m_fovY(XM_PIDIV4),
	  m_aspect(16.0f / 9.0f),
	  m_nearZ(0.1f),
	  m_farZ(1000.0f),
	  m_orthoHeight(25.0f),
	  m_usePerspective(true)
{
	XMStoreFloat4x4(&m_rotation, XMMatrixIdentity());
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
	// 인자 순서는 (pitch, yaw, roll). Transform::GetRotationMatrix 와 같은 함수를 씀.
	XMStoreFloat4x4(&m_rotation, XMMatrixRotationRollPitchYaw(ClampPitch(pitch), WrapAngle(yaw), 0.0f));
	UpdateViewMatrix();
}

void Camera::SetRotation(const XMMATRIX& rotation)
{
	// 이동 성분이 섞여 들어오지 않게 3×3 만 취함. 스케일은 호출자가 이미 벗겨 냈다고 가정함 (CameraComponent::GetPose 참고).
	XMMATRIX r = rotation;
	r.r[0] = XMVectorSetW(r.r[0], 0.0f);
	r.r[1] = XMVectorSetW(r.r[1], 0.0f);
	r.r[2] = XMVectorSetW(r.r[2], 0.0f);
	r.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMStoreFloat4x4(&m_rotation, r);
	UpdateViewMatrix();
}

float Camera::GetYaw() const
{
	// forward = (cos p·sin y, −sin p, cos p·cos y) 를 거꾸로 풀면 yaw = atan2(f.x, f.z). roll 은 forward 를 바꾸지 않으므로 무관함.
	XMFLOAT3 f;
	XMStoreFloat3(&f, GetForward());
	return atan2f(f.x, f.z);
}

float Camera::GetPitch() const
{
	XMFLOAT3 f;
	XMStoreFloat3(&f, GetForward());
	return -asinf(std::clamp(f.y, -1.0f, 1.0f));
}

float Camera::GetRoll() const
{
	// roll 이 0 일 때의 right/up(월드 +Y 기준)을 만들고, 실제 up 이 그 두 축 사이에서 얼마나 돌아 있는지 잼.
	// XMMatrixRotationRollPitchYaw(p, y, r) 로 만든 행렬에 대해 r 을 그대로 돌려줌 (부호까지). 자동 검증 로그와 DebugUI 가 씀.
	const XMVECTOR forward = GetForward();
	const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	if (fabsf(XMVectorGetY(forward)) > 0.9999f) return 0.0f;   // 수직으로 보면 roll 과 yaw 를 구분할 수 없음 (짐벌락). 0 으로 보고함
	const XMVECTOR rightNoRoll = XMVector3Normalize(XMVector3Cross(worldUp, forward));   // 왼손: right = up × forward
	const XMVECTOR upNoRoll = XMVector3Cross(forward, rightNoRoll);
	const XMVECTOR up = GetUp();
	return atan2f(-XMVectorGetX(XMVector3Dot(up, rightNoRoll)), XMVectorGetX(XMVector3Dot(up, upNoRoll)));
}

void Camera::Rotate(float deltaYaw, float deltaPitch)
{
	SetYawPitch(GetYaw() + deltaYaw, GetPitch() + deltaPitch);
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
	SetYawPitch(atan2f(dx, dz), -asinf(std::clamp(dy, -1.0f, 1.0f)));
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
	// forward/up 을 저장 행렬에서 읽으므로 roll 이 있어도 성립함 (LookToLH 는 up 이 forward 와 직교하면 그 기저를 그대로 씀).
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

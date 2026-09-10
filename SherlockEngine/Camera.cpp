#include "pch.h"
#include "Camera.h"

using namespace DirectX;   // 이 파일 안에서만

Camera::Camera() //일단 임의의 기본 값.
	: cameraEye(10, 20, -45),
	  cameraLookAt(0, 5, 0),
	  cameraUp(0.0f, 1.0f, 0.0f),
	  cameraFovY(XM_PIDIV4),
	  cameraAspect(16.0f / 9.0f),
	  cameraNearZ(0.1f),
	  cameraFarZ(1000.0f),
	  cameraOrthoHeight(25.0f),
	  bUsePerspectiveProjection(true),
	  bIsActive(true)
{
	XMStoreFloat4x4(&cameraView, XMMatrixIdentity());
	XMStoreFloat4x4(&cameraProj, XMMatrixIdentity());

	UpdateViewMatrix();
	UpdateProjectionMatrix();
}

Camera::~Camera()
{
}

//카메라 설정
void Camera::SetLookAt(const XMFLOAT3& eye, const XMFLOAT3& lookAt, const XMFLOAT3& up)
{
	cameraEye = eye;
	cameraLookAt = lookAt;
	cameraUp = up;
	UpdateViewMatrix();
}

void Camera::SetLens(float fovY, float aspect, float zn, float zf)
{
	cameraFovY = fovY;
	cameraAspect = aspect;
	cameraNearZ = zn;
	cameraFarZ = zf;
	UpdateProjectionMatrix();
}

void Camera::SetAspectRatio(float aspect)
{
	cameraAspect = aspect;
	UpdateProjectionMatrix();
}

void Camera::SetUsePerspectiveProjection(bool usePerspective)
{
	bUsePerspectiveProjection = usePerspective;
	UpdateProjectionMatrix();
}

//Getter
XMMATRIX Camera::GetViewMatrix() const
{
	return XMLoadFloat4x4(&cameraView);
}

XMMATRIX Camera::GetProjectionMatrix() const
{
	return XMLoadFloat4x4(&cameraProj);
}

XMMATRIX Camera::GetViewProjectionMatrix() const
{
	return GetViewMatrix() * GetProjectionMatrix();
}


//행렬 업데이트
void Camera::UpdateViewMatrix()
{
	XMVECTOR eye = XMLoadFloat3(&cameraEye);
	XMVECTOR lookAt = XMLoadFloat3(&cameraLookAt);
	XMVECTOR up = XMLoadFloat3(&cameraUp);

	XMMATRIX view = XMMatrixLookAtLH(eye, lookAt, up);
	XMStoreFloat4x4(&cameraView, view);
}

void Camera::UpdateProjectionMatrix()
{
	float aspect = cameraAspect;
	if (aspect <= 0.0f)
	{
		aspect = 1.0f;
	}

	XMMATRIX proj = XMMatrixIdentity();
	if (bUsePerspectiveProjection)
	{
		proj = XMMatrixPerspectiveFovLH(cameraFovY, aspect, cameraNearZ, cameraFarZ);
	}
	else
	{
		proj = XMMatrixOrthographicLH(cameraOrthoHeight * aspect, cameraOrthoHeight, cameraNearZ, cameraFarZ);
	}

	XMStoreFloat4x4(&cameraProj, proj);
}
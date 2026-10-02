#include "pch.h"
#include "QuaternionLook.h"
#include "Scene/CameraComponent.h"
#include "Scene/Camera.h"
#include "Core/Log.h"
#include <cmath>

using namespace DirectX;

void QuaternionLook::Reflect(PropertyVisitor& v)
{
	v.Int("mode", mode);
	v.Float("radiusDeg", radiusDeg, 1.0f);
	v.Int("framesPerLoop", framesPerLoop);
	v.Int("logEvery", logEvery);
	v.Float3("startPosition", startPosition);
	v.Float3("lookTarget", lookTarget);
}

void QuaternionLook::Start()
{
	// 이 오브젝트를 활성 카메라로. 같은 씬의 Main Camera(priority 0) 보다 앞서게 함.
	if (CameraComponent* camera = GetComponent<CameraComponent>()) camera->priority = 100;
	SetPosition(startPosition);
	LookAt(lookTarget);   // yaw/pitch 만 설정하고 roll 은 0 — 출발 자세
	XMStoreFloat4(&m_q, XMQuaternionRotationMatrix(GetTransform().GetRotationMatrix()));
	m_frame = 0;
}

void QuaternionLook::Update(float)
{
	// 가상 마우스 증분: 원 위의 이웃한 두 점의 차. 한 바퀴(framesPerLoop 프레임)의 합은 0.
	const int loop = framesPerLoop > 0 ? framesPerLoop : 1;
	const float r = XMConvertToRadians(radiusDeg);
	const float step = XM_2PI / static_cast<float>(loop);
	const float t0 = step * static_cast<float>(m_frame);
	const float t1 = step * static_cast<float>(m_frame + 1);
	const float dYaw = r * (cosf(t1) - cosf(t0));
	const float dPitch = r * (sinf(t1) - sinf(t0));
	++m_frame;

	// XMQuaternionMultiply(a, b) 는 "a 를 먼저, b 를 나중에" 적용하는 회전 (행벡터 행렬곱 a·b 와 같은 순서).
	const XMVECTOR qPitch = XMQuaternionRotationAxis(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), dPitch);
	const XMVECTOR qYaw = XMQuaternionRotationAxis(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), dYaw);
	XMVECTOR q = XMLoadFloat4(&m_q);
	if (mode == 0)
		q = XMQuaternionMultiply(XMQuaternionMultiply(qPitch, qYaw), q);   // 증분이 기존 회전보다 먼저 = 자기 축 기준. roll 이 쌓임
	else
		q = XMQuaternionMultiply(XMQuaternionMultiply(qPitch, q), qYaw);   // pitch 는 먼저(로컬 X), yaw 는 나중(월드 Y). roll 0 유지
	q = XMQuaternionNormalize(q);
	XMStoreFloat4(&m_q, q);

	// 쿼터니언 → 회전 행렬 → 오브젝트 월드 회전. CameraComponent 가 이 월드 행렬을 그대로 카메라에 넘기므로 roll 이 화면에 나타남.
	GetGameObject().SetWorldRotation(XMMatrixRotationQuaternion(q));

	if (logEvery > 0 && m_frame % logEvery == 0)
	{
		XMFLOAT4X4 m;
		XMStoreFloat4x4(&m, XMMatrixRotationQuaternion(q));
		const XMFLOAT3 e = Transform::EulerFromRotationMatrix(m);
		const Camera* camera = GetContext().camera;
		Log::Info("QuaternionLook[mode %d]: 프레임 %d (%.2f 바퀴) yaw %.1f° pitch %.1f° roll %.1f° | Camera::GetRoll %.1f°",
			mode, m_frame, static_cast<float>(m_frame) / static_cast<float>(loop),
			XMConvertToDegrees(e.y), XMConvertToDegrees(e.x), XMConvertToDegrees(e.z),
			camera != nullptr ? XMConvertToDegrees(camera->GetRoll()) : 0.0f);
	}
}

SHERLOCK_SCRIPT(QuaternionLook)

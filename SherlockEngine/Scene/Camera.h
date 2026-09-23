#pragma once
#include <DirectXMath.h>

// 위치 + yaw/pitch 카메라.
//
// 이전 버전은 eye/lookAt/up 세 점을 저장하고 XMMatrixLookAtLH로 뷰행렬을 만들었다.
// "앞으로 3만큼", "오른쪽으로 10도"를 표현하려면 lookAt 점을 매번 다시 계산해야
// 해서 자유 시점 카메라와 맞지 않았다. 지금은 위치와 두 각도만 저장하고,
// 뷰행렬은 카메라의 월드 변환(R·T)의 역행렬로 만든다. 카메라도 결국 씬의
// 물체 하나이고, 뷰행렬은 "그 물체의 로컬 공간으로 가는 변환"이라는 뜻이다.
//
// 관례 (Transform::GetWorldMatrix와 동일):
//   - 행벡터, 왼손 좌표계. R = XMMatrixRotationRollPitchYaw(pitch, yaw, 0) = RotX(pitch)·RotY(yaw)
//   - forward = (0,0,1)·R = (cos p·sin y, −sin p, cos p·cos y)
//   - yaw > 0 이면 오른쪽을 보고, pitch > 0 이면 아래를 본다.
//     마우스 dx > 0 → +yaw, dy > 0 → +pitch 를 부호 뒤집기 없이 그대로 쓸 수 있다.
//   - 롤은 없다. pitch를 ±(90° − ε)로 클램프하므로 짐벌락도 없다.
class Camera
{
public:
	Camera();

	// ---- 자세 ----
	void SetPosition(const DirectX::XMFLOAT3& position);
	const DirectX::XMFLOAT3& GetPosition() const { return m_position; }
	const DirectX::XMFLOAT3& GetEye() const { return m_position; }   // 예전 이름. 조명 상수버퍼가 쓴다.

	void SetYawPitch(float yaw, float pitch);
	float GetYaw() const { return m_yaw; }
	float GetPitch() const { return m_pitch; }

	// 각도 증분(라디안). pitch는 클램프, yaw는 [−π, π]로 감는다.
	void Rotate(float deltaYaw, float deltaPitch);
	// 카메라 기준 전/우 이동과 월드 기준 상 이동. 전방은 pitch를 포함한 3D 방향(비행 카메라).
	void Move(float forward, float right, float up);
	// eye에서 target을 바라보는 yaw/pitch를 계산해 설정한다. up은 항상 +Y.
	void SetLookAt(const DirectX::XMFLOAT3& eye, const DirectX::XMFLOAT3& target);

	DirectX::XMVECTOR GetForward() const;
	DirectX::XMVECTOR GetRight() const;
	DirectX::XMVECTOR GetUp() const;

	// ---- 투영 ----
	void SetLens(float fovY = DirectX::XM_PIDIV4, float aspect = 16.0f / 9.0f, float zn = 0.1f, float zf = 1000.0f);
	void SetAspectRatio(float aspect);
	void SetUsePerspectiveProjection(bool usePerspective);
	bool IsUsePerspectiveProjection() const { return m_usePerspective; }

	DirectX::XMMATRIX GetViewMatrix() const;
	DirectX::XMMATRIX GetProjectionMatrix() const;
	DirectX::XMMATRIX GetViewProjectionMatrix() const;
	float GetFovY() const { return m_fovY; }
	float GetAspect() const { return m_aspect; }
	float GetNearZ() const { return m_nearZ; }
	float GetFarZ() const { return m_farZ; }

private:
	DirectX::XMMATRIX GetRotationMatrix() const;
	void UpdateViewMatrix();
	void UpdateProjectionMatrix();

private:
	DirectX::XMFLOAT3 m_position;
	float m_yaw;     // Y축 회전(라디안)
	float m_pitch;   // X축 회전(라디안)

	DirectX::XMFLOAT4X4 m_view;
	DirectX::XMFLOAT4X4 m_proj;

	float m_fovY;
	float m_aspect;
	float m_nearZ;
	float m_farZ;
	float m_orthoHeight;
	bool m_usePerspective;
};

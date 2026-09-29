#pragma once
#include <DirectXMath.h>

// 위치와 회전 행렬로 자세를 나타내는 카메라.
//
// 이전 버전은 eye/lookAt/up 세 점을 저장하고 XMMatrixLookAtLH로 뷰행렬을 만들었음.
// 이 방식은 "앞으로 3만큼", "오른쪽으로 10도"를 표현하려면 lookAt 점을 매번 다시 계산해야
// 해서 자유 시점 카메라와 맞지 않았음. 2단계부터는 위치와 yaw/pitch 두 각도만 저장했고,
// 뷰행렬은 카메라의 월드 변환(R·T)의 역행렬로 만듦. 카메라도 결국 씬의
// 물체 하나이며, 뷰행렬은 "그 물체의 로컬 공간으로 가는 변환"을 뜻함.
//
// 11-F단계: 저장 형식을 yaw/pitch 두 각도에서 회전 행렬 하나로 바꿈. 각도만 받는 경로에서는 부모 오브젝트의 roll 이
// 카메라에 도달하기 전에 잘려 나갔기 때문 (CameraComponent 가 월드 행렬을 각도 둘로 줄여서 넘겼음). 이제
//   - SetYawPitch(yaw, pitch): 각도로 회전 행렬을 만들어 저장 (에디터 자유 카메라·씬 파일이 쓰는 경로. roll 은 0)
//   - SetRotation(matrix): 회전 행렬을 그대로 저장 (CameraComponent 가 오브젝트 월드 회전을 넘기는 경로. roll 포함)
//   - GetYaw/GetPitch/GetRoll: 저장된 행렬에서 파생. yaw/pitch 는 forward 벡터만으로 정해지므로 roll 이 있어도 안전함.
// 쿼터니언은 저장 형식으로 쓰지 않음 — 회전 합성·보간이 필요한 곳(Scene 의 카메라 블렌드)에서만 임시로 변환해 slerp 함.
//
// 관례 (Transform::GetRotationMatrix 와 동일):
//   - 행벡터, 왼손 좌표계. R = XMMatrixRotationRollPitchYaw(pitch, yaw, roll) = RotZ(roll)·RotX(pitch)·RotY(yaw)
//   - forward = (0,0,1)·R = (cos p·sin y, −sin p, cos p·cos y)  (roll 과 무관)
//   - yaw > 0 이면 오른쪽을 보고, pitch > 0 이면 아래를 봄.
//     따라서 마우스 dx > 0 → +yaw, dy > 0 → +pitch 로 부호를 뒤집지 않고 그대로 쓸 수 있음.
//   - 각도 경로(SetYawPitch/Rotate)는 pitch 를 ±(90° − ε)로 클램프하므로 짐벌락도 없음. 행렬 경로는 클램프하지 않음
//     (부모가 뒤집혀 있으면 카메라도 뒤집힌 채 그대로 보는 것이 맞음).
class Camera
{
public:
	Camera();

	// ---- 자세 ----
	void SetPosition(const DirectX::XMFLOAT3& position);
	const DirectX::XMFLOAT3& GetPosition() const { return m_position; }
	const DirectX::XMFLOAT3& GetEye() const { return m_position; }   // 예전 이름. 조명 상수버퍼에서 사용함.

	// 각도 경로. roll 은 0 이 됨.
	void SetYawPitch(float yaw, float pitch);
	// 행렬 경로 (11-F단계). 순수 회전 행렬(직교 정규)이어야 함 — 스케일이 섞이면 뷰행렬이 늘어남.
	void SetRotation(const DirectX::XMMATRIX& rotation);
	DirectX::XMMATRIX GetRotationMatrix() const { return DirectX::XMLoadFloat4x4(&m_rotation); }
	float GetYaw() const;     // forward 의 수평 방향각. 저장 행렬에서 파생
	float GetPitch() const;   // forward 의 상하각
	float GetRoll() const;    // forward 를 축으로 한 기울기. 각도 경로로 만든 카메라는 0

	// 각도 증분(라디안)만큼 회전함. 현재 yaw/pitch 에 더한 뒤 각도 경로로 다시 저장하므로 roll 이 있었다면 0 이 됨
	// (에디터 자유 카메라 전용. 재생 중 게임 카메라는 이 함수를 쓰지 않음).
	void Rotate(float deltaYaw, float deltaPitch);
	// 카메라 기준 앞/오른쪽 이동과 월드 기준 위쪽 이동. 전방은 pitch까지 포함한 3D 방향임(비행 카메라).
	void Move(float forward, float right, float up);
	// eye에서 target을 바라보는 yaw/pitch를 계산해 설정함. up은 항상 +Y (roll 0).
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
	void UpdateViewMatrix();
	void UpdateProjectionMatrix();

private:
	DirectX::XMFLOAT3 m_position;
	DirectX::XMFLOAT4X4 m_rotation;   // 11-F단계: 순수 회전 행렬. 이전의 m_yaw/m_pitch 를 대신함

	DirectX::XMFLOAT4X4 m_view;
	DirectX::XMFLOAT4X4 m_proj;

	float m_fovY;
	float m_aspect;
	float m_nearZ;
	float m_farZ;
	float m_orthoHeight;
	bool m_usePerspective;
};

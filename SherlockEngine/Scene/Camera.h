#pragma once
#include <DirectXMath.h>
class Camera
{
public:
	Camera();
	~Camera();

	//시야각
	void SetLookAt(const DirectX::XMFLOAT3& eye, const DirectX::XMFLOAT3& lookAt, const DirectX::XMFLOAT3& up = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f));
	//시야 설정
	void SetLens(float fovY = DirectX::XM_PIDIV4, float aspect = 16.0f / 9.0f, float zn = 0.1f, float zf = 1000.0f);
	//화면 비율
	void SetAspectRatio(float aspect);
	//원근/평행 투영 선택
	void SetUsePerspectiveProjection(bool usePerspective);
	bool IsUsePerspectiveProjection() const { return bUsePerspectiveProjection; }

	DirectX::XMMATRIX GetViewMatrix() const;
	DirectX::XMMATRIX GetProjectionMatrix() const;
	DirectX::XMMATRIX GetViewProjectionMatrix() const;

	//카메라 정보 반환
	const DirectX::XMFLOAT3& GetEye() const { return cameraEye; }
	const DirectX::XMFLOAT3& GetLookAt() const { return cameraLookAt; }
	const DirectX::XMFLOAT3& GetUp() const { return cameraUp; }

	//활성화 관련 코드
	void SetActive(bool active) { bIsActive = active; }
	bool IsActive() const { return bIsActive; }

private:
	void UpdateViewMatrix();
	void UpdateProjectionMatrix();

private:
	DirectX::XMFLOAT3 cameraEye;
	DirectX::XMFLOAT3 cameraLookAt;
	DirectX::XMFLOAT3 cameraUp;

	//View행렬
	DirectX::XMFLOAT4X4 cameraView;

	//Proj행렬
	DirectX::XMFLOAT4X4 cameraProj;

	//카메라 정보들
	float cameraFovY;
	float cameraAspect;
	float cameraNearZ;
	float cameraFarZ;
	float cameraOrthoHeight;

	//원근 투영을 사용할지 여부. false면 평행 투영.
	bool bUsePerspectiveProjection;

	//활정화 되어 있는지 여부
	bool bIsActive;
};
#pragma once
#include <DirectXMath.h>

class Transform
{
public:
	Transform();

	void SetPosition(const DirectX::XMFLOAT3& value);
	void SetPosition(float x, float y, float z);
	void SetRotation(const DirectX::XMFLOAT3& value);
	void SetRotation(float x, float y, float z);
	void SetScale(const DirectX::XMFLOAT3& value);
	void SetScale(float x, float y, float z);

	const DirectX::XMFLOAT3& GetPosition() const { return position; }
	const DirectX::XMFLOAT3& GetRotation() const { return rotation; }
	const DirectX::XMFLOAT3& GetScale() const { return scale; }

	// 9단계: S·R·T 앞에 곱하는 고정 행렬. 모델 노드의 월드 변환(임의 행렬)을 오일러 각으로 분해하지 않고
	// 그대로 둔다. world = pre × S × R × T. 인스턴스 전체를 옮기고 싶으면 position/rotation/scale 을 쓴다.
	void SetPreTransform(const DirectX::XMMATRIX& value);
	DirectX::XMMATRIX GetPreTransform() const { return DirectX::XMLoadFloat4x4(&preTransform); }
	void ClearPreTransform() { hasPreTransform = false; }
	bool HasPreTransform() const { return hasPreTransform; }

	DirectX::XMMATRIX GetWorldMatrix() const;

	// ---- 11-C단계: 스크립트가 Update 에서 부르는 조작 함수 (유니티 Transform 의 Translate/Rotate/LookAt) ----
	void Translate(float x, float y, float z);                       // 월드 축 기준 이동
	void Translate(const DirectX::XMFLOAT3& delta);
	void TranslateLocal(float forward, float right, float up);       // 자기 회전 기준 이동 (앞/오른쪽/위)
	void Rotate(float pitch, float yaw, float roll);                 // 오일러 각 누적 (라디안)
	void LookAt(const DirectX::XMFLOAT3& target);                    // target 을 향하도록 yaw/pitch 설정, roll 0 (Camera::SetLookAt 규약)
	DirectX::XMVECTOR GetForward() const;                            // 회전만 적용한 월드 방향 (+Z 가 앞)
	DirectX::XMVECTOR GetRight() const;
	DirectX::XMVECTOR GetUp() const;

private:
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 rotation;
	DirectX::XMFLOAT3 scale;
	DirectX::XMFLOAT4X4 preTransform;
	bool hasPreTransform = false;
};

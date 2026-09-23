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

private:
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 rotation;
	DirectX::XMFLOAT3 scale;
	DirectX::XMFLOAT4X4 preTransform;
	bool hasPreTransform = false;
};

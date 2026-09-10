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

	DirectX::XMMATRIX GetWorldMatrix() const;

private:
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 rotation;
	DirectX::XMFLOAT3 scale;
};

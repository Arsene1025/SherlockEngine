#pragma once

class Transform
{
public:
	Transform();

	void SetPosition(const XMFLOAT3& value);
	void SetPosition(float x, float y, float z);
	void SetRotation(const XMFLOAT3& value);
	void SetRotation(float x, float y, float z);
	void SetScale(const XMFLOAT3& value);
	void SetScale(float x, float y, float z);

	const XMFLOAT3& GetPosition() const { return position; }
	const XMFLOAT3& GetRotation() const { return rotation; }
	const XMFLOAT3& GetScale() const { return scale; }

	XMMATRIX GetWorldMatrix() const;

private:
	XMFLOAT3 position;
	XMFLOAT3 rotation;
	XMFLOAT3 scale;
};

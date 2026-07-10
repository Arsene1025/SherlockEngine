#pragma once

class Device;
class Shader;
class Camera;
class Mesh;

class MeshRenderer
{
public:
	MeshRenderer();
	~MeshRenderer();

	bool Initialize(Device* device, Shader* shader, Mesh* mesh);
	void Release();
	void UpdateConstantBuffer(const XMMATRIX& world, Camera* camera);
	void Draw();

private:
	Device* graphicsDevice = nullptr;
	Shader* graphicsShader = nullptr;
	Mesh* mesh = nullptr;

	ID3D11Buffer* vertexBuffer = nullptr;
	ID3D11Buffer* indexBuffer = nullptr;
	ID3D11InputLayout* inputLayout = nullptr;
};

#pragma once
#include <DirectXMath.h>

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
	void UpdateConstantBuffer(const DirectX::XMMATRIX& world, Camera* camera);
	void Draw();

private:
	Device* graphicsDevice = nullptr;
	Shader* graphicsShader = nullptr;
	Mesh* mesh = nullptr;

	ComPtr<ID3D11Buffer> vertexBuffer;
	ComPtr<ID3D11Buffer> indexBuffer;
	ComPtr<ID3D11InputLayout> inputLayout;
};

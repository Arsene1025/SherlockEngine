#pragma once
#include <DirectXMath.h>

class Device;
class Shader;
class Camera;
class Mesh;

// Mesh의 GPU 쪽 절반: 정점/인덱스 버퍼와 드로우.
//
// 입력 레이아웃은 더 이상 여기 없다. 1단계부터 PSO가 정점 레이아웃을 갖는다.
// 3단계에서 Buffer 핸들 두 개짜리 GpuMesh 캐시로 바뀌고 이 클래스는 사라진다.
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
};

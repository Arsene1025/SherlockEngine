#pragma once
#include "enum.h"     // RS_MAX_, RM_DEFAULT, MAX_LIGHTS
#include "struct.h"   // LightData
#include "GameObject.h"
#include "Mesh.h"
#include "MeshRenderer.h"

class Device;
class Shader;
class Camera;

class Renderer
{
public:
	Renderer();
	~Renderer();

	bool Initialize(Device* device, Shader* shader, Camera* camera);

	bool DataLoading();
	void DataRelease();
	void Render();
	void RenderModeUpdate();
	void UpdateGUI();

	bool ObjLoad();
	void ObjRelease();
	void ObjUpdate();
	void ObjDraw();
	void UpdateLightConstantBuffer();

	bool RasterStateCreate();
	void RasterStateRelease();

public:
	BOOL g_bCullback = FALSE;
	BOOL g_bWireFrame = FALSE;

	ComPtr<ID3D11RasterizerState> g_RState[RS_MAX_];
	DWORD g_RMode = RM_DEFAULT;

private:
	Device* graphicsDevice = nullptr;
	Shader* graphicsShader = nullptr;
	Camera* mainCamera = nullptr;

	GameObject sphereObject;
	Mesh sphereMesh;
	MeshRenderer sphereRenderer;

	LightData lights[MAX_LIGHTS];
	UINT lightCount = 1;
	DirectX::XMFLOAT3 ambientColor = DirectX::XMFLOAT3(0.12f, 0.12f, 0.12f);
	DirectX::XMFLOAT3 specularColor = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	float shininess = 32.0f;
};

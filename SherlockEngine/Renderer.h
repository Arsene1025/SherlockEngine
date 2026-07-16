#pragma once
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

	int DataLoading();
	void DataRelease();
	void Render();
	void RenderModeUpdate();
	void UpdateGUI();

	int ObjLoad();
	void ObjRelease();
	void ObjUpdate();
	void ObjDraw();
	void UpdateLightConstantBuffer();

	void RasterStateCreate();
	void RasterStateRelease();

public:
	BOOL g_bCullback = FALSE;
	BOOL g_bWireFrame = FALSE;

	XMFLOAT4 g_ClearColor = XMFLOAT4(0.0f, 0.125f, 0.3f, 1.0f);

	ID3D11RasterizerState* g_RState[RS_MAX_] = { NULL, };
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
	XMFLOAT3 ambientColor = XMFLOAT3(0.12f, 0.12f, 0.12f);
	XMFLOAT3 specularColor = XMFLOAT3(1.0f, 1.0f, 1.0f);
	float shininess = 32.0f;
};

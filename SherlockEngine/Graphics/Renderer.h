#pragma once
#include "Graphics/enum.h"     // MAX_LIGHTS
#include "Graphics/struct.h"   // LightData
#include "Graphics/Handle.h"
#include "Graphics/PipelineTypes.h"
#include "Scene/GameObject.h"
#include "Graphics/Mesh.h"
#include "Graphics/MeshRenderer.h"

class Device;
class Shader;
class Camera;

// ImGui에서 바꾸는 렌더 설정. 매 프레임 이 값으로 PipelineStateDesc를 만들고
// 캐시에서 PSO를 얻는다. 예전의 RM_* 비트 플래그와 RasterizerState 4개를 대체한다.
struct RenderSettings
{
	bool wireframe = false;
	bool cullBack = true;
};

class Renderer
{
public:
	Renderer();
	~Renderer();

	bool Initialize(Device* device, Shader* shader, Camera* camera);

	bool DataLoading();
	void DataRelease();
	void Render();
	void UpdateGUI();

	RenderSettings& GetSettings() { return settings; }

	// 2단계 임시 접근자. 3단계에서 Scene이 오브젝트를 소유하면 사라진다.
	GameObject& GetObject(int index) { return objects[index]; }

private:
	bool ObjLoad();
	void ObjRelease();
	void UpdateLightConstantBuffer();

	// settings에 맞는 PSO 핸들. 캐시가 있으므로 매 프레임 불러도 된다.
	PipelineHandle GetPipelineForSettings();

private:
	Device* graphicsDevice = nullptr;
	Shader* graphicsShader = nullptr;
	Camera* mainCamera = nullptr;

	RenderSettings settings;
	PipelineStateDesc baseDesc;   // 셰이더·정점 레이아웃 등 고정 부분

	// 1단계 검증용 씬: 같은 메시로 서로 관통하는 구 두 개.
	static constexpr int kObjectCount = 2;
	GameObject objects[kObjectCount];
	Mesh sphereMesh;
	MeshRenderer sphereRenderer;

	LightData lights[MAX_LIGHTS];
	UINT lightCount = 1;
	DirectX::XMFLOAT3 ambientColor = DirectX::XMFLOAT3(0.12f, 0.12f, 0.12f);
	DirectX::XMFLOAT3 specularColor = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	float shininess = 32.0f;
};

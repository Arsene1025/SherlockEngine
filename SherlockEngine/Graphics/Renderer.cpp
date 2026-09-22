#include "pch.h"
#include "Graphics/Renderer.h"
#include "Graphics/D3D11/Device.h"
#include "Graphics/D3D11/Shader.h"
#include "Graphics/VertexTypes.h"
#include "Scene/Camera.h"
#include "Core/Log.h"
#include <imgui.h>

using namespace DirectX;   // 이 파일 안에서만

Renderer::Renderer()
{
}

Renderer::~Renderer()
{
	DataRelease();
}

bool Renderer::Initialize(Device* device, Shader* shader, Camera* camera)
{
	if (device == nullptr || shader == nullptr || camera == nullptr) return false;

	graphicsDevice = device;
	graphicsShader = shader;
	mainCamera = camera;

	return true;
}

bool Renderer::DataLoading()
{
	if (!ObjLoad())
	{
		Log::Error("DataLoading : 오브젝트 로드 실패.");
		return false;
	}

	// PSO의 고정 부분. 셰이더 핸들과 정점 레이아웃은 설정과 무관하다.
	// 나머지(깊이 테스트 켬, 불투명, 트라이앵글 리스트)는 Desc의 기본값이다.
	baseDesc = PipelineStateDesc{};
	baseDesc.vs = graphicsShader->GetVS();
	baseDesc.ps = graphicsShader->GetPS();
	baseDesc.vertexLayout = GetVertexLayoutPCN();

	// 첫 프레임 전에 기본 PSO를 만들어 두어 생성 실패를 초기화 단계에서 잡는다.
	if (!GetPipelineForSettings().IsValid())
	{
		Log::Error("DataLoading : 기본 PSO 생성 실패.");
		return false;
	}

	return true;
}

void Renderer::DataRelease()
{
	ObjRelease();
}

void Renderer::Render()
{
	if (graphicsDevice == nullptr || mainCamera == nullptr) return;

	// 파이프라인 상태는 매 프레임 PSO 하나로 설정한다.
	// ImGui의 DX11 백엔드가 자기 상태를 복원하긴 하지만, 그것에 기대지 않는다.
	graphicsDevice->BindPipeline(GetPipelineForSettings());

	UpdateLightConstantBuffer();

	for (int i = 0; i < kObjectCount; ++i)
	{
		sphereRenderer.UpdateConstantBuffer(objects[i].GetTransform().GetWorldMatrix(), mainCamera);
		sphereRenderer.Draw();
	}
}

PipelineHandle Renderer::GetPipelineForSettings()
{
	PipelineStateDesc desc = baseDesc;
	desc.rasterizer.fill = settings.wireframe ? FillMode::Wireframe : FillMode::Solid;
	desc.rasterizer.cull = settings.cullBack ? CullMode::Back : CullMode::None;
	return graphicsDevice->CreatePipeline(desc);
}

void Renderer::UpdateGUI()
{
	ImGui::Separator();
	ImGui::Text("Pipeline");
	ImGui::Checkbox("Wireframe", &settings.wireframe);
	ImGui::Checkbox("Cull back faces", &settings.cullBack);
	bool vsync = graphicsDevice->IsVSync();
	if (ImGui::Checkbox("VSync", &vsync))
	{
		graphicsDevice->SetVSync(vsync);
	}
	ImGui::Text("PSO cache: %zu", graphicsDevice->GetPipelineCount());

	//조명 정보를 업데이트
	static const char* lightTypeNames[] = { "Directional", "Point", "Spot" };
	int selectedType = static_cast<int>(lights[0].type);

	ImGui::Separator();
	ImGui::Text("Light");
	if (ImGui::Combo("Type", &selectedType, lightTypeNames, ARRAYSIZE(lightTypeNames)))
	{
		lights[0].type = static_cast<UINT>(selectedType);
	}

	ImGui::SliderFloat3("Position", &lights[0].position.x, -30.0f, 30.0f);
	ImGui::DragFloat3("Direction", &lights[0].direction.x, 0.01f, -1.0f, 1.0f);
}

bool Renderer::ObjLoad()
{
	sphereMesh = Mesh::CreateSphere(5.0f, 32, 16, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));

	// 반지름 5짜리 구 두 개를 x = ±3에 두면 서로 관통한다.
	// 깊이 버퍼가 제대로 동작하면 교선이 깨끗한 원호로 보인다.
	objects[0].GetTransform().SetPosition(-3.0f, 5.0f, 0.0f);
	objects[1].GetTransform().SetPosition(3.0f, 5.0f, 0.0f);

	if (!sphereRenderer.Initialize(graphicsDevice, graphicsShader, &sphereMesh))
	{
		Log::Error("ObjLoad : 구 MeshRenderer 초기화 실패.");
		return false;
	}

	return true;
}

void Renderer::ObjRelease()
{
	sphereRenderer.Release();
}

void Renderer::UpdateLightConstantBuffer()
{
	if (graphicsDevice == nullptr || graphicsShader == nullptr || mainCamera == nullptr ||
		graphicsShader->GetLightCBBuffer() == nullptr)
	{
		return;
	}

	LightBuffer lightBuffer = {};
	lightBuffer.lightCount = (lightCount < MAX_LIGHTS) ? lightCount : MAX_LIGHTS;
	lightBuffer.cameraPosition = mainCamera->GetEye();
	lightBuffer.ambientColor = ambientColor;
	lightBuffer.specularColor = specularColor;
	lightBuffer.shininess = shininess;

	for (UINT i = 0; i < lightBuffer.lightCount; ++i)
	{
		lightBuffer.lights[i] = lights[i];

		const XMVECTOR direction = XMLoadFloat3(&lightBuffer.lights[i].direction);
		const float lengthSquared = XMVectorGetX(XMVector3LengthSq(direction));
		if (lengthSquared > 0.000001f)
		{
			XMStoreFloat3(&lightBuffer.lights[i].direction, XMVector3Normalize(direction));
		}
		else
		{
			lightBuffer.lights[i].direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
		}
	}

	graphicsDevice->GetContext()->UpdateSubresource(
		graphicsShader->GetLightCBBuffer(), 0, nullptr, &lightBuffer, 0, 0);
}

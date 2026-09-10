#include "pch.h"
#include "Renderer.h"
#include "Device.h"
#include "Shader.h"
#include "Camera.h"
#include "Log.h"
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

	if (!RasterStateCreate())
	{
		return false;
	}

	return true;
}

void Renderer::DataRelease()
{
	ObjRelease();
	RasterStateRelease();
}

void Renderer::Render()
{
	ObjUpdate();
	ObjDraw();
}

void Renderer::UpdateGUI()
{
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

bool Renderer::RasterStateCreate()
{
	if (graphicsDevice == nullptr)
	{
		Log::Error("RasterStateCreate : Device가 없음.");
		return false;
	}

	D3D11_RASTERIZER_DESC rd = {};
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_NONE;
	rd.FrontCounterClockwise = false;
	rd.DepthBias = 0;
	rd.DepthBiasClamp = 0;
	rd.SlopeScaledDepthBias = 0;
	rd.DepthClipEnable = true;
	rd.ScissorEnable = false;
	rd.MultisampleEnable = true;
	rd.AntialiasedLineEnable = true;

	// 네 상태를 같은 방식으로 만든다. 하나라도 실패하면 렌더 모드가 깨지므로 중단.
	struct StateDesc { int slot; D3D11_FILL_MODE fill; D3D11_CULL_MODE cull; const char* name; };
	const StateDesc descs[] =
	{
		{ RS_SOLID,        D3D11_FILL_SOLID,     D3D11_CULL_NONE, "RS_SOLID" },
		{ RS_WIREFRM,      D3D11_FILL_WIREFRAME, D3D11_CULL_NONE, "RS_WIREFRM" },
		{ RS_CULLBACK,     D3D11_FILL_SOLID,     D3D11_CULL_BACK, "RS_CULLBACK" },
		{ RS_WIRECULLBACK, D3D11_FILL_WIREFRAME, D3D11_CULL_BACK, "RS_WIRECULLBACK" },
	};

	for (const StateDesc& d : descs)
	{
		rd.FillMode = d.fill;
		rd.CullMode = d.cull;

		HRESULT hr = graphicsDevice->GetDevice()->CreateRasterizerState(
			&rd, g_RState[d.slot].ReleaseAndGetAddressOf());

		if (FAILED(hr))
		{
			Log::Error("RasterizerState 생성 실패 : %s. %s", d.name, Log::HrToString(hr).c_str());
			return false;
		}
	}

	return true;
}

void Renderer::RasterStateRelease()
{
	for (int i = 0; i < RS_MAX_; i++)
	{
		g_RState[i].Reset();
	}
}

void Renderer::RenderModeUpdate()
{
	switch (g_RMode)
	{
	default:
	case RM_SOLID:
		graphicsDevice->GetContext()->RSSetState(g_RState[RS_SOLID].Get());
		break;
	case RM_WIREFRAME:
		graphicsDevice->GetContext()->RSSetState(g_RState[RS_WIREFRM].Get());
		break;
	case RM_CULLBACK:
		graphicsDevice->GetContext()->RSSetState(g_RState[RS_CULLBACK].Get());
		break;
	case RM_WIREFRAME | RM_CULLBACK:
		graphicsDevice->GetContext()->RSSetState(g_RState[RS_WIRECULLBACK].Get());
		break;
	}
}

bool Renderer::ObjLoad()
{
	sphereMesh = Mesh::CreateSphere(5.0f, 32, 16, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
	sphereObject.GetTransform().SetPosition(0.0f, 5.0f, 0.0f);
	sphereObject.GetTransform().SetScale(1.0f, 1.0f, 1.0f);

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

void Renderer::ObjUpdate()
{
	sphereRenderer.UpdateConstantBuffer(sphereObject.GetTransform().GetWorldMatrix(), mainCamera);
	UpdateLightConstantBuffer();
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

void Renderer::ObjDraw()
{
	sphereRenderer.Draw();
}

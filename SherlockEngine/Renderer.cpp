#include "pch.h"
#include "Renderer.h"
#include "Device.h"
#include "Shader.h"
#include "Camera.h"

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

int Renderer::DataLoading()
{
	if (ObjLoad() == 0)
	{
		return 0;
	}

	RasterStateCreate();

	return 1;
}

void Renderer::DataRelease()
{
	ObjRelease();
	RasterStateRelease();
}

void Renderer::Render()
{
	ObjUpdate();
	graphicsDevice->Clear();
	ObjDraw();
}

void Renderer::RasterStateCreate()
{
	D3D11_RASTERIZER_DESC rd;
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
	graphicsDevice->GetDevice()->CreateRasterizerState(&rd, &g_RState[RS_SOLID]);

	rd.FillMode = D3D11_FILL_WIREFRAME;
	rd.CullMode = D3D11_CULL_NONE;
	graphicsDevice->GetDevice()->CreateRasterizerState(&rd, &g_RState[RS_WIREFRM]);

	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_BACK;
	graphicsDevice->GetDevice()->CreateRasterizerState(&rd, &g_RState[RS_CULLBACK]);

	rd.FillMode = D3D11_FILL_WIREFRAME;
	rd.CullMode = D3D11_CULL_BACK;
	graphicsDevice->GetDevice()->CreateRasterizerState(&rd, &g_RState[RS_WIRECULLBACK]);
}

void Renderer::RasterStateRelease()
{
	for (int i = 0; i < RS_MAX_; i++)
	{
		SafeRelease(g_RState[i]);
	}
}

void Renderer::RenderModeUpdate()
{
	switch (g_RMode)
	{
	default:
	case RM_SOLID:
		graphicsDevice->GetContext()->RSSetState(g_RState[RS_SOLID]);
		break;
	case RM_WIREFRAME:
		graphicsDevice->GetContext()->RSSetState(g_RState[RS_WIREFRM]);
		break;
	case RM_CULLBACK:
		graphicsDevice->GetContext()->RSSetState(g_RState[RS_CULLBACK]);
		break;
	case RM_WIREFRAME | RM_CULLBACK:
		graphicsDevice->GetContext()->RSSetState(g_RState[RS_WIRECULLBACK]);
		break;
	}
}

int Renderer::ObjLoad()
{
	sphereMesh = Mesh::CreateSphere(5.0f, 32, 16, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
	sphereObject.GetTransform().SetPosition(0.0f, 5.0f, 0.0f);
	sphereObject.GetTransform().SetScale(1.0f, 1.0f, 1.0f);

	return sphereRenderer.Initialize(graphicsDevice, graphicsShader, &sphereMesh) ? 1 : 0;
}

void Renderer::ObjRelease()
{
	sphereRenderer.Release();
}

void Renderer::ObjUpdate()
{
	sphereRenderer.UpdateConstantBuffer(sphereObject.GetTransform().GetWorldMatrix(), mainCamera);
}

void Renderer::ObjDraw()
{
	sphereRenderer.Draw();
}
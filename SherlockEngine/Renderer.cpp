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

}

bool Renderer::Initialize(Device* device, Shader* shader, Camera* camera)
{
	if (device == nullptr || shader == nullptr || camera == nullptr) return false;

	graphicsDevice = device;
	graphicsShader = shader;
    mainCamera = camera;

	//DataLoading();
	return true;
}

#pragma region D3D수업자료 기반 코드(추후 변경, 분리 예정)
int Renderer::DataLoading()
{
	ObjLoad();
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
	//graphicsDevice->Present();
}

void Renderer::RasterStateCreate()
{
	//상태1
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
	//레스터라이져 객체 생성.
	graphicsDevice->GetDevice()->CreateRasterizerState(&rd, &g_RState[RS_SOLID]);


	//상태2
	rd.FillMode = D3D11_FILL_WIREFRAME;
	rd.CullMode = D3D11_CULL_NONE;
	graphicsDevice->GetDevice()->CreateRasterizerState(&rd, &g_RState[RS_WIREFRM]);

	//상태3
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_BACK;
	graphicsDevice->GetDevice()->CreateRasterizerState(&rd, &g_RState[RS_CULLBACK]);

	//상태4
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
	VERTEX	verts[] = {
		{ -10.0f,  0.0f, 0.0f, 1, 0, 0, 1 },
		{   0.0f, 10.0f, 0.0f, 0, 1, 0, 1 },
		{  10.0f,  0.0f, 0.0f, 0, 1, 1, 1 },
		{ -6.0f,  8.0f, 0.0f,  0, 0.5f, 1, 1 },
		{  0.0f, 18.0f, 0.0f,  1, 1.0f, 1, 1 }, 
		{  6.0f,  8.0f, 0.0f,  0, 0.5f, 1, 1 },
		{ 0.0f,  0.0f, 10.0f,  1, 1, 0, 1 },
		{ 0.0f, 10.0f,  0.0f,  0, 1, 0, 1 }, 
		{ 0.0f,  0.0f,-10.0f,  1, 1, 0, 1 },
	};
	graphicsDevice->CreateVertexBuffer(verts, sizeof(verts), sizeof(VERTEX), &g_vertexBuffer);

	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	graphicsDevice->CreateInputLayout(layout, ARRAYSIZE(layout), graphicsShader->GetVSCode(), &g_vertexBufferLayout);
	return 0;
}

void Renderer::ObjRelease()
{
	SafeRelease(g_vertexBuffer);
	SafeRelease(g_vertexBufferLayout);
}

void Renderer::ObjUpdate()
{

	//상수 버퍼 갱신
	ConstBuffer cb = {};
	XMMATRIX world = XMMatrixIdentity();
	XMMATRIX view = mainCamera->GetViewMatrix();
	XMMATRIX proj = mainCamera->GetProjectionMatrix();

	XMMATRIX wvp = world * view * proj;

	cb.mWorld = XMMatrixTranspose(world);
	cb.mView = XMMatrixTranspose(view);
	cb.mProj = XMMatrixTranspose(proj);
	cb.mWVP = XMMatrixTranspose(wvp);

	//상수 버퍼 업데이트 -> 일단 여기서 처리
	graphicsDevice->GetContext()->UpdateSubresource(graphicsShader->GetCBBuffer(), 0, nullptr, &cb, 0, 0);

}

void Renderer::ObjDraw()
{
	UINT stride = sizeof(VERTEX);
	UINT offset = 0;
	//Vertex버퍼 설정
	graphicsDevice->GetContext()->IASetVertexBuffers(0, 1, &g_vertexBuffer, &stride, &offset);
	//입력 레이아웃 설정
	graphicsDevice->GetContext()->IASetInputLayout(g_vertexBufferLayout);
	//기하 구조 설정
	graphicsDevice->GetContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	//여기서 셰이더 설정


	//그리기
	graphicsDevice->GetContext()->Draw(9, 0);
}
#pragma endregion




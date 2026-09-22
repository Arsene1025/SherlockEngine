#include "pch.h"
#include "Graphics/MeshRenderer.h"
#include "Graphics/D3D11/Device.h"
#include "Graphics/D3D11/Shader.h"
#include "Scene/Camera.h"
#include "Graphics/VertexTypes.h"   // VERTEX
#include "Graphics/struct.h"        // ConstBuffer
#include "Graphics/Mesh.h"

using namespace DirectX;   // 이 파일 안에서만

MeshRenderer::MeshRenderer()
{
}

MeshRenderer::~MeshRenderer()
{
	Release();
}

bool MeshRenderer::Initialize(Device* device, Shader* shader, Mesh* sourceMesh)
{
	if (device == nullptr || shader == nullptr || sourceMesh == nullptr || !sourceMesh->IsValid())
	{
		return false;
	}

	graphicsDevice = device;
	graphicsShader = shader;
	mesh = sourceMesh;

	const std::vector<VERTEX>& vertices = mesh->GetVertices();
	const std::vector<UINT>& indices = mesh->GetIndices();

	vertexBuffer = graphicsDevice->CreateVertexBuffer(
		vertices.data(),
		static_cast<UINT>(sizeof(VERTEX) * vertices.size()),
		sizeof(VERTEX));

	indexBuffer = graphicsDevice->CreateIndexBuffer(
		indices.data(),
		static_cast<UINT>(sizeof(UINT) * indices.size()));

	return vertexBuffer != nullptr && indexBuffer != nullptr;
}

void MeshRenderer::Release()
{
	vertexBuffer.Reset();
	indexBuffer.Reset();

	graphicsDevice = nullptr;
	graphicsShader = nullptr;
	mesh = nullptr;
}

void MeshRenderer::UpdateConstantBuffer(const XMMATRIX& world, Camera* camera)
{
	if (graphicsDevice == nullptr || graphicsShader == nullptr || camera == nullptr)
	{
		return;
	}

	ConstBuffer cb = {};
	XMMATRIX view = camera->GetViewMatrix();
	XMMATRIX proj = camera->GetProjectionMatrix();
	XMMATRIX wvp = world * view * proj;

	cb.mWorld = XMMatrixTranspose(world);
	const XMMATRIX worldInverseTranspose = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
	cb.mWorldInverseTranspose = XMMatrixTranspose(worldInverseTranspose);
	cb.mView = XMMatrixTranspose(view);
	cb.mProj = XMMatrixTranspose(proj);
	cb.mWVP = XMMatrixTranspose(wvp);

	graphicsDevice->GetContext()->UpdateSubresource(graphicsShader->GetCBBuffer(), 0, nullptr, &cb, 0, 0);
}

void MeshRenderer::Draw()
{
	if (graphicsDevice == nullptr || mesh == nullptr || vertexBuffer == nullptr || indexBuffer == nullptr)
	{
		return;
	}

	UINT stride = sizeof(VERTEX);
	UINT offset = 0;

	// 입력 레이아웃과 토폴로지는 PSO가 이미 설정했다. 여기는 버퍼와 드로우만.
	graphicsDevice->GetContext()->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
	graphicsDevice->GetContext()->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	graphicsDevice->GetContext()->DrawIndexed(mesh->GetIndexCount(), 0, 0);
}

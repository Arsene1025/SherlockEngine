#include "pch.h"
#include "MeshRenderer.h"
#include "Device.h"
#include "Shader.h"
#include "Camera.h"
#include "Mesh.h"

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

	graphicsDevice->CreateVertexBuffer(
		const_cast<VERTEX*>(vertices.data()),
		static_cast<UINT>(sizeof(VERTEX) * vertices.size()),
		sizeof(VERTEX),
		&vertexBuffer);

	graphicsDevice->CreateIndexBuffer(
		const_cast<UINT*>(indices.data()),
		static_cast<UINT>(sizeof(UINT) * indices.size()),
		&indexBuffer);

	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	graphicsDevice->CreateInputLayout(layout, ARRAYSIZE(layout), graphicsShader->GetVSCode(), &inputLayout);

	return vertexBuffer != nullptr && indexBuffer != nullptr && inputLayout != nullptr;
}

void MeshRenderer::Release()
{
	SafeRelease(vertexBuffer);
	SafeRelease(indexBuffer);
	SafeRelease(inputLayout);

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
	cb.mView = XMMatrixTranspose(view);
	cb.mProj = XMMatrixTranspose(proj);
	cb.mWVP = XMMatrixTranspose(wvp);

	graphicsDevice->GetContext()->UpdateSubresource(graphicsShader->GetCBBuffer(), 0, nullptr, &cb, 0, 0);
}

void MeshRenderer::Draw()
{
	if (graphicsDevice == nullptr || mesh == nullptr || vertexBuffer == nullptr || indexBuffer == nullptr || inputLayout == nullptr)
	{
		return;
	}

	UINT stride = sizeof(VERTEX);
	UINT offset = 0;

	graphicsDevice->GetContext()->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
	graphicsDevice->GetContext()->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
	graphicsDevice->GetContext()->IASetInputLayout(inputLayout);
	graphicsDevice->GetContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	graphicsDevice->GetContext()->DrawIndexed(mesh->GetIndexCount(), 0, 0);
}
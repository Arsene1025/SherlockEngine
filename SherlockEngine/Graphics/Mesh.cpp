#include "pch.h"
#include <cmath>
#include "Graphics/Mesh.h"

using namespace DirectX;   // 이 파일 안에서만

Mesh Mesh::CreateSphere(float radius, UINT sliceCount, UINT stackCount, const XMFLOAT4& color)
{
	Mesh mesh;

	if (sliceCount < 3)
	{
		sliceCount = 3;
	}
	if (stackCount < 2)
	{
		stackCount = 2;
	}

	const float phiStep = XM_PI / stackCount;
	const float thetaStep = XM_2PI / sliceCount;

	mesh.vertices.push_back({ 0.0f, radius, 0.0f, color.x, color.y, color.z, color.w, 0.0f, 1.0f, 0.0f });

	for (UINT i = 1; i <= stackCount - 1; ++i)
	{
		const float phi = i * phiStep;

		for (UINT j = 0; j <= sliceCount; ++j)
		{
			const float theta = j * thetaStep;
			const float nx = sinf(phi) * cosf(theta);
			const float ny = cosf(phi);
			const float nz = sinf(phi) * sinf(theta);
			const float x = radius * nx;
			const float y = radius * ny;
			const float z = radius * nz;

			mesh.vertices.push_back({ x, y, z, color.x, color.y, color.z, color.w, nx, ny, nz });
		}
	}

	mesh.vertices.push_back({ 0.0f, -radius, 0.0f, color.x, color.y, color.z, color.w, 0.0f, -1.0f, 0.0f });

	for (UINT i = 1; i <= sliceCount; ++i)
	{
		mesh.indices.push_back(0);
		mesh.indices.push_back(i + 1);
		mesh.indices.push_back(i);
	}

	const UINT baseIndex = 1;
	const UINT ringVertexCount = sliceCount + 1;
	for (UINT i = 0; i < stackCount - 2; ++i)
	{
		for (UINT j = 0; j < sliceCount; ++j)
		{
			mesh.indices.push_back(baseIndex + i * ringVertexCount + j);
			mesh.indices.push_back(baseIndex + i * ringVertexCount + j + 1);
			mesh.indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);

			mesh.indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);
			mesh.indices.push_back(baseIndex + i * ringVertexCount + j + 1);
			mesh.indices.push_back(baseIndex + (i + 1) * ringVertexCount + j + 1);
		}
	}

	const UINT southPoleIndex = static_cast<UINT>(mesh.vertices.size() - 1);
	const UINT bottomBaseIndex = southPoleIndex - ringVertexCount;
	for (UINT i = 0; i < sliceCount; ++i)
	{
		mesh.indices.push_back(southPoleIndex);
		mesh.indices.push_back(bottomBaseIndex + i);
		mesh.indices.push_back(bottomBaseIndex + i + 1);
	}
	return mesh;
}
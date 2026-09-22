#include "pch.h"
#include <cmath>
#include "Graphics/Mesh.h"

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	VERTEX MakeVertex(float x, float y, float z, float nx, float ny, float nz, const XMFLOAT4& c)
	{
		return VERTEX{ x, y, z, c.x, c.y, c.z, c.w, nx, ny, nz };
	}
}

Mesh Mesh::CreateSphere(float radius, uint32_t sliceCount, uint32_t stackCount, const XMFLOAT4& color)
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

	for (uint32_t i = 1; i <= stackCount - 1; ++i)
	{
		const float phi = i * phiStep;

		for (uint32_t j = 0; j <= sliceCount; ++j)
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

	for (uint32_t i = 1; i <= sliceCount; ++i)
	{
		mesh.indices.push_back(0);
		mesh.indices.push_back(i + 1);
		mesh.indices.push_back(i);
	}

	const uint32_t baseIndex = 1;
	const uint32_t ringVertexCount = sliceCount + 1;
	for (uint32_t i = 0; i < stackCount - 2; ++i)
	{
		for (uint32_t j = 0; j < sliceCount; ++j)
		{
			mesh.indices.push_back(baseIndex + i * ringVertexCount + j);
			mesh.indices.push_back(baseIndex + i * ringVertexCount + j + 1);
			mesh.indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);

			mesh.indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);
			mesh.indices.push_back(baseIndex + i * ringVertexCount + j + 1);
			mesh.indices.push_back(baseIndex + (i + 1) * ringVertexCount + j + 1);
		}
	}

	const uint32_t southPoleIndex = static_cast<uint32_t>(mesh.vertices.size() - 1);
	const uint32_t bottomBaseIndex = southPoleIndex - ringVertexCount;
	for (uint32_t i = 0; i < sliceCount; ++i)
	{
		mesh.indices.push_back(southPoleIndex);
		mesh.indices.push_back(bottomBaseIndex + i);
		mesh.indices.push_back(bottomBaseIndex + i + 1);
	}
	return mesh;
}

Mesh Mesh::CreateCube(float size, const XMFLOAT4& color)
{
	Mesh mesh;
	const float h = size * 0.5f;

	// 면마다 정점 4개. 정점을 공유하면 노멀을 평균 내야 해서 모서리가 둥글게 보인다.
	// 순서: 앞(−Z) 뒤(+Z) 위(+Y) 아래(−Y) 왼(−X) 오른(+X). 각 면의 인덱스는 (0,1,2)(0,2,3).
	mesh.vertices =
	{
		// 앞면 (z = −h), 노멀 −Z
		MakeVertex(-h, -h, -h,  0, 0, -1, color), MakeVertex(-h, +h, -h,  0, 0, -1, color),
		MakeVertex(+h, +h, -h,  0, 0, -1, color), MakeVertex(+h, -h, -h,  0, 0, -1, color),
		// 뒷면 (z = +h), 노멀 +Z
		MakeVertex(-h, -h, +h,  0, 0, +1, color), MakeVertex(+h, -h, +h,  0, 0, +1, color),
		MakeVertex(+h, +h, +h,  0, 0, +1, color), MakeVertex(-h, +h, +h,  0, 0, +1, color),
		// 윗면 (y = +h), 노멀 +Y
		MakeVertex(-h, +h, -h,  0, +1, 0, color), MakeVertex(-h, +h, +h,  0, +1, 0, color),
		MakeVertex(+h, +h, +h,  0, +1, 0, color), MakeVertex(+h, +h, -h,  0, +1, 0, color),
		// 아랫면 (y = −h), 노멀 −Y
		MakeVertex(-h, -h, -h,  0, -1, 0, color), MakeVertex(+h, -h, -h,  0, -1, 0, color),
		MakeVertex(+h, -h, +h,  0, -1, 0, color), MakeVertex(-h, -h, +h,  0, -1, 0, color),
		// 왼쪽면 (x = −h), 노멀 −X
		MakeVertex(-h, -h, +h, -1, 0, 0, color), MakeVertex(-h, +h, +h, -1, 0, 0, color),
		MakeVertex(-h, +h, -h, -1, 0, 0, color), MakeVertex(-h, -h, -h, -1, 0, 0, color),
		// 오른쪽면 (x = +h), 노멀 +X
		MakeVertex(+h, -h, -h, +1, 0, 0, color), MakeVertex(+h, +h, -h, +1, 0, 0, color),
		MakeVertex(+h, +h, +h, +1, 0, 0, color), MakeVertex(+h, -h, +h, +1, 0, 0, color),
	};

	mesh.indices.reserve(36);
	for (uint32_t face = 0; face < 6; ++face)
	{
		const uint32_t b = face * 4;
		mesh.indices.push_back(b + 0); mesh.indices.push_back(b + 1); mesh.indices.push_back(b + 2);
		mesh.indices.push_back(b + 0); mesh.indices.push_back(b + 2); mesh.indices.push_back(b + 3);
	}
	return mesh;
}

Mesh Mesh::CreatePlane(float width, float depth, uint32_t m, uint32_t n, const XMFLOAT4& color)
{
	Mesh mesh;
	if (m < 2) m = 2;
	if (n < 2) n = 2;

	const float halfWidth = 0.5f * width;
	const float halfDepth = 0.5f * depth;
	const float dx = width / (n - 1);
	const float dz = depth / (m - 1);

	// i는 z(뒤에서 앞으로), j는 x(왼쪽에서 오른쪽으로).
	mesh.vertices.reserve(static_cast<size_t>(m) * n);
	for (uint32_t i = 0; i < m; ++i)
	{
		const float z = halfDepth - i * dz;
		for (uint32_t j = 0; j < n; ++j)
		{
			const float x = -halfWidth + j * dx;
			mesh.vertices.push_back(MakeVertex(x, 0.0f, z, 0.0f, 1.0f, 0.0f, color));
		}
	}

	mesh.indices.reserve(static_cast<size_t>(m - 1) * (n - 1) * 6);
	for (uint32_t i = 0; i < m - 1; ++i)
	{
		for (uint32_t j = 0; j < n - 1; ++j)
		{
			mesh.indices.push_back(i * n + j);
			mesh.indices.push_back(i * n + j + 1);
			mesh.indices.push_back((i + 1) * n + j);

			mesh.indices.push_back((i + 1) * n + j);
			mesh.indices.push_back(i * n + j + 1);
			mesh.indices.push_back((i + 1) * n + j + 1);
		}
	}
	return mesh;
}

Mesh Mesh::CreateCylinder(float bottomRadius, float topRadius, float height,
	uint32_t sliceCount, uint32_t stackCount, const XMFLOAT4& color)
{
	Mesh mesh;
	if (sliceCount < 3) sliceCount = 3;
	if (stackCount < 1) stackCount = 1;

	const float stackHeight = height / stackCount;
	const float radiusStep = (topRadius - bottomRadius) / stackCount;
	const uint32_t ringCount = stackCount + 1;
	const float dTheta = XM_2PI / sliceCount;

	// 옆면. 링마다 sliceCount+1개(첫 정점을 끝에 복제해 텍스처 이음새를 만든다).
	for (uint32_t i = 0; i < ringCount; ++i)
	{
		const float y = -0.5f * height + i * stackHeight;
		const float r = bottomRadius + i * radiusStep;

		for (uint32_t j = 0; j <= sliceCount; ++j)
		{
			const float c = cosf(j * dTheta);
			const float s = sinf(j * dTheta);

			// 노멀 = 접선(T) × 종접선(B). 원뿔대에서도 옆면에 수직이 된다.
			const XMVECTOR tangent = XMVectorSet(-s, 0.0f, c, 0.0f);
			const float dr = bottomRadius - topRadius;
			const XMVECTOR bitangent = XMVectorSet(dr * c, -height, dr * s, 0.0f);
			XMFLOAT3 normal;
			XMStoreFloat3(&normal, XMVector3Normalize(XMVector3Cross(tangent, bitangent)));

			mesh.vertices.push_back(MakeVertex(r * c, y, r * s, normal.x, normal.y, normal.z, color));
		}
	}

	const uint32_t ringVertexCount = sliceCount + 1;
	for (uint32_t i = 0; i < stackCount; ++i)
	{
		for (uint32_t j = 0; j < sliceCount; ++j)
		{
			mesh.indices.push_back(i * ringVertexCount + j);
			mesh.indices.push_back((i + 1) * ringVertexCount + j);
			mesh.indices.push_back((i + 1) * ringVertexCount + j + 1);

			mesh.indices.push_back(i * ringVertexCount + j);
			mesh.indices.push_back((i + 1) * ringVertexCount + j + 1);
			mesh.indices.push_back(i * ringVertexCount + j + 1);
		}
	}

	// 윗뚜껑
	{
		const uint32_t baseIndex = static_cast<uint32_t>(mesh.vertices.size());
		const float y = 0.5f * height;
		for (uint32_t i = 0; i <= sliceCount; ++i)
		{
			const float x = topRadius * cosf(i * dTheta);
			const float z = topRadius * sinf(i * dTheta);
			mesh.vertices.push_back(MakeVertex(x, y, z, 0.0f, 1.0f, 0.0f, color));
		}
		mesh.vertices.push_back(MakeVertex(0.0f, y, 0.0f, 0.0f, 1.0f, 0.0f, color));
		const uint32_t centerIndex = static_cast<uint32_t>(mesh.vertices.size() - 1);
		for (uint32_t i = 0; i < sliceCount; ++i)
		{
			mesh.indices.push_back(centerIndex);
			mesh.indices.push_back(baseIndex + i + 1);
			mesh.indices.push_back(baseIndex + i);
		}
	}

	// 아랫뚜껑
	{
		const uint32_t baseIndex = static_cast<uint32_t>(mesh.vertices.size());
		const float y = -0.5f * height;
		for (uint32_t i = 0; i <= sliceCount; ++i)
		{
			const float x = bottomRadius * cosf(i * dTheta);
			const float z = bottomRadius * sinf(i * dTheta);
			mesh.vertices.push_back(MakeVertex(x, y, z, 0.0f, -1.0f, 0.0f, color));
		}
		mesh.vertices.push_back(MakeVertex(0.0f, y, 0.0f, 0.0f, -1.0f, 0.0f, color));
		const uint32_t centerIndex = static_cast<uint32_t>(mesh.vertices.size() - 1);
		for (uint32_t i = 0; i < sliceCount; ++i)
		{
			mesh.indices.push_back(centerIndex);
			mesh.indices.push_back(baseIndex + i);
			mesh.indices.push_back(baseIndex + i + 1);
		}
	}

	return mesh;
}

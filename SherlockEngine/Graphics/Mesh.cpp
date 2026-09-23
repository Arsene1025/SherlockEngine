#include "pch.h"
#include <cmath>
#include <cfloat>
#include "Graphics/Mesh.h"

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	// 탄젠트 프레임을 채운다. T = u 증가 방향, B = v 증가 방향 (둘 다 해석적으로 안다).
	// T 는 노멀에 직교화(Gram-Schmidt)하고, w 는 cross(N, T) 가 B 와 같은 쪽이면 +1, 반대면 −1.
	// 픽셀 셰이더는 B = cross(N, T) * w 로 되살린다. 이 규칙은 정점 포맷의 일부다 (VertexTypes.h).
	VERTEX MakeVertex(float x, float y, float z, float nx, float ny, float nz, float u, float v,
		const XMFLOAT3& tangent, const XMFLOAT3& bitangent, const XMFLOAT4& c)
	{
		VERTEX vertex{ x, y, z, c.x, c.y, c.z, c.w, nx, ny, nz, u, v, 1.0f, 0.0f, 0.0f, 1.0f };

		const XMVECTOR n = XMVector3Normalize(XMVectorSet(nx, ny, nz, 0.0f));
		XMVECTOR t = XMLoadFloat3(&tangent);
		t = XMVector3Normalize(XMVectorSubtract(t, XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, t)))));
		const XMVECTOR b = XMLoadFloat3(&bitangent);
		const float handedness = (XMVectorGetX(XMVector3Dot(XMVector3Cross(n, t), b)) < 0.0f) ? -1.0f : 1.0f;

		XMFLOAT3 tOut;
		XMStoreFloat3(&tOut, t);
		vertex.tx = tOut.x; vertex.ty = tOut.y; vertex.tz = tOut.z; vertex.tw = handedness;
		return vertex;
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

	// UV: u = 경도(theta / 2π), v = 위도(phi / π). 극점은 u = 0.5.
	// T = dP/dθ (경도 방향), B = dP/dφ (위도 방향, 아래로).
	mesh.vertices.push_back(MakeVertex(0.0f, radius, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.0f,
		XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, -1.0f), color));

	for (uint32_t i = 1; i <= stackCount - 1; ++i)
	{
		const float phi = i * phiStep;

		for (uint32_t j = 0; j <= sliceCount; ++j)
		{
			const float theta = j * thetaStep;
			const float nx = sinf(phi) * cosf(theta);
			const float ny = cosf(phi);
			const float nz = sinf(phi) * sinf(theta);

			const XMFLOAT3 tangent(-sinf(theta), 0.0f, cosf(theta));
			const XMFLOAT3 bitangent(cosf(phi) * cosf(theta), -sinf(phi), cosf(phi) * sinf(theta));
			mesh.vertices.push_back(MakeVertex(radius * nx, radius * ny, radius * nz, nx, ny, nz,
				theta / XM_2PI, phi / XM_PI, tangent, bitangent, color));
		}
	}

	mesh.vertices.push_back(MakeVertex(0.0f, -radius, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 1.0f,
		XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), color));

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
	mesh.Finalize();
	return mesh;
}

Mesh Mesh::CreateCube(float size, const XMFLOAT4& color)
{
	Mesh mesh;
	const float h = size * 0.5f;

	// 면마다 정점 4개. 정점을 공유하면 노멀을 평균 내야 해서 모서리가 둥글게 보인다.
	// 순서: 앞(−Z) 뒤(+Z) 위(+Y) 아래(−Y) 왼(−X) 오른(+X). 각 면의 인덱스는 (0,1,2)(0,2,3).
	// UV는 면마다 (0,0)-(1,1). 면의 첫 정점이 (0,1), 둘째가 (0,0)이 되도록 시계방향으로 돈다.
	// T/B 는 각 면에서 u+/v+ 가 향하는 월드 방향이다.
	struct Face { XMFLOAT3 n, t, b; XMFLOAT3 p[4]; float uv[4][2]; };
	const Face faces[6] =
	{
		{ { 0,0,-1 }, { 1,0,0 },  { 0,-1,0 }, { {-h,-h,-h}, {-h,+h,-h}, {+h,+h,-h}, {+h,-h,-h} }, { {0,1},{0,0},{1,0},{1,1} } },   // 앞
		{ { 0,0,+1 }, { -1,0,0 }, { 0,-1,0 }, { {-h,-h,+h}, {+h,-h,+h}, {+h,+h,+h}, {-h,+h,+h} }, { {1,1},{0,1},{0,0},{1,0} } },   // 뒤
		{ { 0,+1,0 }, { 1,0,0 },  { 0,0,-1 }, { {-h,+h,-h}, {-h,+h,+h}, {+h,+h,+h}, {+h,+h,-h} }, { {0,1},{0,0},{1,0},{1,1} } },   // 위
		{ { 0,-1,0 }, { -1,0,0 }, { 0,0,-1 }, { {-h,-h,-h}, {+h,-h,-h}, {+h,-h,+h}, {-h,-h,+h} }, { {1,1},{0,1},{0,0},{1,0} } },   // 아래
		{ { -1,0,0 }, { 0,0,-1 }, { 0,-1,0 }, { {-h,-h,+h}, {-h,+h,+h}, {-h,+h,-h}, {-h,-h,-h} }, { {0,1},{0,0},{1,0},{1,1} } },   // 왼
		{ { +1,0,0 }, { 0,0,+1 }, { 0,-1,0 }, { {+h,-h,-h}, {+h,+h,-h}, {+h,+h,+h}, {+h,-h,+h} }, { {0,1},{0,0},{1,0},{1,1} } },   // 오른
	};

	mesh.vertices.reserve(24);
	mesh.indices.reserve(36);
	for (uint32_t face = 0; face < 6; ++face)
	{
		const Face& f = faces[face];
		for (uint32_t i = 0; i < 4; ++i)
		{
			mesh.vertices.push_back(MakeVertex(f.p[i].x, f.p[i].y, f.p[i].z, f.n.x, f.n.y, f.n.z,
				f.uv[i][0], f.uv[i][1], f.t, f.b, color));
		}
		const uint32_t b = face * 4;
		mesh.indices.push_back(b + 0); mesh.indices.push_back(b + 1); mesh.indices.push_back(b + 2);
		mesh.indices.push_back(b + 0); mesh.indices.push_back(b + 2); mesh.indices.push_back(b + 3);
	}
	mesh.Finalize();
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
	const float du = 1.0f / (n - 1);
	const float dv = 1.0f / (m - 1);

	// i는 z(뒤에서 앞으로), j는 x(왼쪽에서 오른쪽으로). UV는 (0,0)이 뒤-왼쪽. T = +X, B = −Z.
	const XMFLOAT3 tangent(1.0f, 0.0f, 0.0f);
	const XMFLOAT3 bitangent(0.0f, 0.0f, -1.0f);
	mesh.vertices.reserve(static_cast<size_t>(m) * n);
	for (uint32_t i = 0; i < m; ++i)
	{
		const float z = halfDepth - i * dz;
		for (uint32_t j = 0; j < n; ++j)
		{
			const float x = -halfWidth + j * dx;
			mesh.vertices.push_back(MakeVertex(x, 0.0f, z, 0.0f, 1.0f, 0.0f, j * du, i * dv, tangent, bitangent, color));
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
	mesh.Finalize();
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
	// UV: u는 둘레(0..1), v는 위에서 아래로(위 0, 아래 1).
	for (uint32_t i = 0; i < ringCount; ++i)
	{
		const float y = -0.5f * height + i * stackHeight;
		const float r = bottomRadius + i * radiusStep;

		for (uint32_t j = 0; j <= sliceCount; ++j)
		{
			const float c = cosf(j * dTheta);
			const float s = sinf(j * dTheta);

			// 노멀 = 접선(T) × 종접선(B). 원뿔대에서도 옆면에 수직이 된다.
			// B 는 v 가 증가하는(아래로 내려가는) 방향이라 (dr·c, −height, dr·s).
			const XMFLOAT3 tangent(-s, 0.0f, c);
			const float dr = bottomRadius - topRadius;
			const XMFLOAT3 bitangent(dr * c, -height, dr * s);
			XMFLOAT3 normal;
			XMStoreFloat3(&normal, XMVector3Normalize(XMVector3Cross(XMLoadFloat3(&tangent), XMLoadFloat3(&bitangent))));

			mesh.vertices.push_back(MakeVertex(r * c, y, r * s, normal.x, normal.y, normal.z,
				static_cast<float>(j) / sliceCount, 1.0f - static_cast<float>(i) / stackCount, tangent, bitangent, color));
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

	// 뚜껑. UV는 원판을 위에서 본 평면 투영 (u = 0.5 + 0.5·cos, v = 0.5 − 0.5·sin): T = +X, B = −Z.
	const XMFLOAT3 capTangent(1.0f, 0.0f, 0.0f);
	const XMFLOAT3 capBitangent(0.0f, 0.0f, -1.0f);

	// 윗뚜껑
	{
		const uint32_t baseIndex = static_cast<uint32_t>(mesh.vertices.size());
		const float y = 0.5f * height;
		for (uint32_t i = 0; i <= sliceCount; ++i)
		{
			const float x = topRadius * cosf(i * dTheta);
			const float z = topRadius * sinf(i * dTheta);
			mesh.vertices.push_back(MakeVertex(x, y, z, 0.0f, 1.0f, 0.0f, 0.5f + 0.5f * cosf(i * dTheta), 0.5f - 0.5f * sinf(i * dTheta), capTangent, capBitangent, color));
		}
		mesh.vertices.push_back(MakeVertex(0.0f, y, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, capTangent, capBitangent, color));
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
			mesh.vertices.push_back(MakeVertex(x, y, z, 0.0f, -1.0f, 0.0f, 0.5f + 0.5f * cosf(i * dTheta), 0.5f - 0.5f * sinf(i * dTheta), capTangent, capBitangent, color));
		}
		mesh.vertices.push_back(MakeVertex(0.0f, y, 0.0f, 0.0f, -1.0f, 0.0f, 0.5f, 0.5f, capTangent, capBitangent, color));
		const uint32_t centerIndex = static_cast<uint32_t>(mesh.vertices.size() - 1);
		for (uint32_t i = 0; i < sliceCount; ++i)
		{
			mesh.indices.push_back(centerIndex);
			mesh.indices.push_back(baseIndex + i);
			mesh.indices.push_back(baseIndex + i + 1);
		}
	}

	mesh.Finalize();
	return mesh;
}

// ------------------------------------------------------------------ 9단계: 외부 데이터·탄젠트·바운드

float Bounds::Radius() const
{
	const XMFLOAT3 e = Extent();
	return sqrtf(e.x * e.x + e.y * e.y + e.z * e.z);
}

Mesh Mesh::FromData(std::vector<VERTEX>&& vertices, std::vector<uint32_t>&& indices, std::vector<Submesh>&& submeshes)
{
	Mesh mesh;
	mesh.vertices = std::move(vertices);
	mesh.indices = std::move(indices);
	mesh.submeshes = std::move(submeshes);
	mesh.Finalize();
	return mesh;
}

void Mesh::Finalize()
{
	if (submeshes.empty())
	{
		submeshes.push_back(Submesh{ 0, static_cast<uint32_t>(indices.size()), 0 });
	}

	if (vertices.empty())
	{
		bounds = Bounds{};
		return;
	}
	XMVECTOR lo = XMVectorReplicate(FLT_MAX);
	XMVECTOR hi = XMVectorReplicate(-FLT_MAX);
	for (const VERTEX& v : vertices)
	{
		const XMVECTOR p = XMVectorSet(v.x, v.y, v.z, 0.0f);
		lo = XMVectorMin(lo, p);
		hi = XMVectorMax(hi, p);
	}
	XMStoreFloat3(&bounds.min, lo);
	XMStoreFloat3(&bounds.max, hi);
}

void Mesh::ComputeTangents(std::vector<VERTEX>& vertices, const std::vector<uint32_t>& indices, bool bitangentTowardsIncreasingV)
{
	// Lengyel, "Computing Tangent Space Basis Vectors for an Arbitrary Mesh".
	// 삼각형마다 (dP/du, dP/dv) 를 풀어 정점에 누적하고, 마지막에 노멀에 직교화한다.
	std::vector<XMFLOAT3> tan1(vertices.size(), XMFLOAT3(0.0f, 0.0f, 0.0f));
	std::vector<XMFLOAT3> tan2(vertices.size(), XMFLOAT3(0.0f, 0.0f, 0.0f));

	for (size_t i = 0; i + 2 < indices.size(); i += 3)
	{
		const uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
		if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;
		const VERTEX& v0 = vertices[i0];
		const VERTEX& v1 = vertices[i1];
		const VERTEX& v2 = vertices[i2];

		const float e1x = v1.x - v0.x, e1y = v1.y - v0.y, e1z = v1.z - v0.z;
		const float e2x = v2.x - v0.x, e2y = v2.y - v0.y, e2z = v2.z - v0.z;
		const float du1 = v1.u - v0.u, dv1 = v1.v - v0.v;
		const float du2 = v2.u - v0.u, dv2 = v2.v - v0.v;

		const float det = du1 * dv2 - du2 * dv1;
		if (fabsf(det) < 1e-12f) continue;   // UV 가 퇴화한 삼각형은 건너뛴다
		const float r = 1.0f / det;

		// dP/du, dP/dv
		const XMFLOAT3 sdir((e1x * dv2 - e2x * dv1) * r, (e1y * dv2 - e2y * dv1) * r, (e1z * dv2 - e2z * dv1) * r);
		const XMFLOAT3 tdir((e2x * du1 - e1x * du2) * r, (e2y * du1 - e1y * du2) * r, (e2z * du1 - e1z * du2) * r);

		for (uint32_t index : { i0, i1, i2 })
		{
			tan1[index].x += sdir.x; tan1[index].y += sdir.y; tan1[index].z += sdir.z;
			tan2[index].x += tdir.x; tan2[index].y += tdir.y; tan2[index].z += tdir.z;
		}
	}

	for (size_t i = 0; i < vertices.size(); ++i)
	{
		VERTEX& v = vertices[i];
		const XMVECTOR n = XMVector3Normalize(XMVectorSet(v.nx, v.ny, v.nz, 0.0f));
		XMVECTOR t = XMLoadFloat3(&tan1[i]);
		if (XMVectorGetX(XMVector3LengthSq(t)) < 1e-16f)
		{
			// 누적된 것이 없으면(UV 없음 등) 노멀에 수직인 아무 벡터.
			const XMVECTOR axis = fabsf(v.ny) < 0.99f ? XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f) : XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
			t = XMVector3Cross(axis, n);
		}
		// Gram-Schmidt: 노멀 성분을 뺀다.
		t = XMVector3Normalize(XMVectorSubtract(t, XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, t)))));

		// 손잡이. cross(N, T) 가 dP/dv 와 같은 쪽이면 +1. glTF 는 B 가 −dP/dv 를 가리켜야 하므로 부호를 뒤집는다.
		const XMVECTOR b = XMLoadFloat3(&tan2[i]);
		float w = (XMVectorGetX(XMVector3Dot(XMVector3Cross(n, t), b)) < 0.0f) ? -1.0f : 1.0f;
		if (!bitangentTowardsIncreasingV) w = -w;

		XMFLOAT3 out;
		XMStoreFloat3(&out, t);
		v.tx = out.x; v.ty = out.y; v.tz = out.z; v.tw = w;
	}
}

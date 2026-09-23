#pragma once
#include "Graphics/VertexTypes.h"   // VERTEX
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

// 서브메시: 인덱스 범위 하나 + 재질 슬롯. 9단계: glTF 의 mesh 는 primitive 여러 개(재질마다 하나)이고,
// 정점·인덱스 버퍼는 하나로 합쳐 두고 범위만 나눈다. 재질 슬롯 번호는 GameObject 가 실제 Material 로 푼다.
struct Submesh
{
	uint32_t indexStart = 0;
	uint32_t indexCount = 0;
	uint32_t materialSlot = 0;
};

// 축 정렬 바운딩 박스 (로컬 공간).
struct Bounds
{
	DirectX::XMFLOAT3 min = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	DirectX::XMFLOAT3 max = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

	DirectX::XMFLOAT3 Center() const { return DirectX::XMFLOAT3((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f); }
	DirectX::XMFLOAT3 Extent() const { return DirectX::XMFLOAT3((max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f); }
	float Radius() const;   // 중심에서 모서리까지
};

// CPU 쪽 메시: 정점 배열과 인덱스 배열. GPU 버퍼는 갖지 않는다.
//
// 프리미티브 생성기는 Frank Luna의 GeometryGenerator 관례를 따른다:
// 왼손 좌표계, 시계방향이 앞면(RasterizerDesc::frontCounterClockwise = false),
// 노멀은 바깥쪽. 인덱스는 CPU 에서 32비트로 들고, Renderer 가 정점 수에 따라 16/32비트 버퍼를 고른다 (D10).
class Mesh
{
public:
	static Mesh CreateSphere(float radius, uint32_t sliceCount, uint32_t stackCount, const DirectX::XMFLOAT4& color);
	// 한 변의 길이 size. 면마다 정점 4개(면 노멀) → 24정점 36인덱스.
	static Mesh CreateCube(float size, const DirectX::XMFLOAT4& color);
	// XZ 평면, 노멀 +Y. m×n 정점 격자.
	static Mesh CreatePlane(float width, float depth, uint32_t m, uint32_t n, const DirectX::XMFLOAT4& color);
	// Y축 원기둥(원뿔대). bottomRadius == topRadius 이면 원기둥.
	static Mesh CreateCylinder(float bottomRadius, float topRadius, float height,
		uint32_t sliceCount, uint32_t stackCount, const DirectX::XMFLOAT4& color);

	// 9단계: 외부 데이터(모델 로더)로 만든다. submeshes 가 비면 전체가 서브메시 하나(슬롯 0).
	static Mesh FromData(std::vector<VERTEX>&& vertices, std::vector<uint32_t>&& indices, std::vector<Submesh>&& submeshes = {});

	// 탄젠트가 없는 정점 배열에 삼각형별 UV 미분으로 탄젠트를 채운다 (Lengyel). 노멀은 이미 있어야 한다.
	// bitangentTowardsIncreasingV: true 면 B = dP/dv (엔진 프리미티브·DirectX 식 노멀 맵: 초록 = v 증가),
	// false 면 B = −dP/dv (glTF/OpenGL 식 노멀 맵: 초록 = 위). 결과는 tangent.w 의 부호로만 다르다.
	static void ComputeTangents(std::vector<VERTEX>& vertices, const std::vector<uint32_t>& indices, bool bitangentTowardsIncreasingV);

	const std::vector<VERTEX>& GetVertices() const { return vertices; }
	const std::vector<uint32_t>& GetIndices() const { return indices; }
	uint32_t GetVertexCount() const { return static_cast<uint32_t>(vertices.size()); }
	uint32_t GetIndexCount() const { return static_cast<uint32_t>(indices.size()); }
	bool IsValid() const { return !vertices.empty() && !indices.empty(); }

	// 항상 하나 이상. 프리미티브는 전체를 덮는 서브메시 하나.
	const std::vector<Submesh>& GetSubmeshes() const { return submeshes; }
	const Bounds& GetBounds() const { return bounds; }

	// 정점 레이아웃은 PSO의 속성이다. Renderer가 GetVertexLayout(format)으로 Desc를 채운다.
	VertexFormat GetVertexFormat() const { return vertexFormat; }

private:
	// 서브메시 기본값과 바운딩 박스를 채운다. 모든 생성 경로의 마지막에 부른다.
	void Finalize();

	std::vector<VERTEX> vertices;
	std::vector<uint32_t> indices;
	std::vector<Submesh> submeshes;
	Bounds bounds;
	VertexFormat vertexFormat = VertexFormat::PositionColorNormalTexcoordTangent;
};

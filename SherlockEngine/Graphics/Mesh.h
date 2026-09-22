#pragma once
#include "Graphics/VertexTypes.h"   // VERTEX
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

// CPU 쪽 메시: 정점 배열과 인덱스 배열. GPU 버퍼는 갖지 않는다.
//
// 프리미티브 생성기는 Frank Luna의 GeometryGenerator 관례를 따른다:
// 왼손 좌표계, 시계방향이 앞면(RasterizerDesc::frontCounterClockwise = false),
// 노멀은 바깥쪽. 인덱스는 32비트.
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

	const std::vector<VERTEX>& GetVertices() const { return vertices; }
	const std::vector<uint32_t>& GetIndices() const { return indices; }
	uint32_t GetIndexCount() const { return static_cast<uint32_t>(indices.size()); }
	bool IsValid() const { return !vertices.empty() && !indices.empty(); }

private:
	std::vector<VERTEX> vertices;
	std::vector<uint32_t> indices;
};

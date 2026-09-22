#pragma once
#include "Graphics/struct.h"   // VERTEX
#include <vector>

class Mesh
{
public:
	static Mesh CreateSphere(float radius, UINT sliceCount, UINT stackCount, const DirectX::XMFLOAT4& color);

	const std::vector<VERTEX>& GetVertices() const { return vertices; }
	const std::vector<UINT>& GetIndices() const { return indices; }
	UINT GetIndexCount() const { return static_cast<UINT>(indices.size()); }
	bool IsValid() const { return !vertices.empty() && !indices.empty(); }

private:
	std::vector<VERTEX> vertices;
	std::vector<UINT> indices;
};

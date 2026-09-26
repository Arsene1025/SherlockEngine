#pragma once
#include <cstddef>
#include "RHI/PipelineTypes.h"

// 정점 구조체와 그 레이아웃 기술.
//
// 레이아웃은 offsetof로 만듦. 예전에는 MeshRenderer가 D3D11_INPUT_ELEMENT_DESC에
// 오프셋 0/12/28을 손으로 적었음. 구조체를 고치면 그 숫자도 같이 고쳐야 했고,
// 잊으면 컴파일은 되는데 화면이 깨졌음.
//
// 4단계(D9): 정점 포맷을 열거형으로 두고 Mesh가 자기 포맷을 알려줌. Renderer는
// GetVertexLayout(mesh.GetVertexFormat())으로 PSO Desc를 채움.
// 5단계: UV 추가. 6단계: 탄젠트 추가 (Position / Color / Normal / TexCoord / Tangent, 64바이트).

struct VERTEX
{
	float x, y, z; 			//좌표(Position)
	float r, g, b, a;		//색상(Diffuse Color)
	float nx, ny, nz;		//노멀(Normal)
	float u, v;				//텍스처 좌표(TexCoord). (0,0) = 왼쪽 위 (D3D 관례)
	float tx, ty, tz, tw;	//탄젠트(Tangent) = u 가 증가하는 방향. w = 손잡이(±1): B = cross(N, T) * w
};
using VertexPCNTT = VERTEX;

enum class VertexFormat : uint8_t
{
	PositionColorNormalTexcoordTangent,   // VERTEX (VertexPCNTT)
};

// Position / Color / Normal / TexCoord / Tangent 레이아웃.
inline VertexLayoutDesc GetVertexLayoutPCNTT()
{
	VertexLayoutDesc layout;
	layout.attributes[0] = { VertexSemantic::Position, 0, Format::R32G32B32_FLOAT,    static_cast<uint16_t>(offsetof(VERTEX, x)),  0 };
	layout.attributes[1] = { VertexSemantic::Color,    0, Format::R32G32B32A32_FLOAT, static_cast<uint16_t>(offsetof(VERTEX, r)),  0 };
	layout.attributes[2] = { VertexSemantic::Normal,   0, Format::R32G32B32_FLOAT,    static_cast<uint16_t>(offsetof(VERTEX, nx)), 0 };
	layout.attributes[3] = { VertexSemantic::TexCoord, 0, Format::R32G32_FLOAT,       static_cast<uint16_t>(offsetof(VERTEX, u)),  0 };
	layout.attributes[4] = { VertexSemantic::Tangent,  0, Format::R32G32B32A32_FLOAT, static_cast<uint16_t>(offsetof(VERTEX, tx)), 0 };
	layout.attributeCount = 5;
	layout.stride = sizeof(VERTEX);
	return layout;
}

inline VertexLayoutDesc GetVertexLayout(VertexFormat format)
{
	switch (format)
	{
	case VertexFormat::PositionColorNormalTexcoordTangent:
	default:
		return GetVertexLayoutPCNTT();
	}
}

inline uint32_t GetVertexStride(VertexFormat format)
{
	return GetVertexLayout(format).stride;
}

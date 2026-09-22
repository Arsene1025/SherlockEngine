#pragma once
#include <cstddef>
#include "Graphics/PipelineTypes.h"

// 정점 구조체와 그 레이아웃 기술.
//
// 레이아웃은 offsetof로 만든다. 예전에는 MeshRenderer가 D3D11_INPUT_ELEMENT_DESC에
// 오프셋 0/12/28을 손으로 적었다. 구조체를 고치면 그 숫자를 같이 고쳐야 했고,
// 잊으면 컴파일은 되는데 화면만 깨졌다.

//Vertex구조체
struct VERTEX
{
	float x, y, z; 			//좌표(Position)
	float r, g, b, a;		//색상(Diffuse Color)
	float nx, ny, nz;		//노멀(Normal)
};

// Position / Color / Normal 레이아웃.
inline VertexLayoutDesc GetVertexLayoutPCN()
{
	VertexLayoutDesc layout;
	layout.attributes[0] = { VertexSemantic::Position, 0, Format::R32G32B32_FLOAT,    static_cast<uint16_t>(offsetof(VERTEX, x)),  0 };
	layout.attributes[1] = { VertexSemantic::Color,    0, Format::R32G32B32A32_FLOAT, static_cast<uint16_t>(offsetof(VERTEX, r)),  0 };
	layout.attributes[2] = { VertexSemantic::Normal,   0, Format::R32G32B32_FLOAT,    static_cast<uint16_t>(offsetof(VERTEX, nx)), 0 };
	layout.attributeCount = 3;
	layout.stride = sizeof(VERTEX);
	return layout;
}

#pragma once
#include <cstddef>
#include "RHI/PipelineTypes.h"

// 정점 구조체와 그 레이아웃 기술.
//
// 레이아웃은 offsetof로 만듦. 예전에는 MeshRenderer가 D3D11_INPUT_ELEMENT_DESC에
// 오프셋 0/12/28을 손으로 적었음. 구조체를 고치면 그 숫자도 같이 고쳐야 했고,
// 잊으면 컴파일은 되는데 화면이 깨졌음.
//
// 정점 포맷을 열거형으로 두고 Mesh가 자기 포맷을 알려줌. Renderer는
// GetVertexLayout(mesh.GetVertexFormat())으로 PSO Desc를 채움.
// 현재 포맷은 Position / Color / Normal / TexCoord / Tangent (64바이트).
//
// C++20: 레이아웃 생성 함수를 consteval 로 바꿈. 이 함수는 런타임 입력이 없고
// offsetof / sizeof 만 쓰므로 컴파일 시점에 계산되어야 마땅하고, consteval 은 그것을 강제함
// (런타임 호출은 컴파일 에러). 결과는 아래 kVertexLayoutPCNTT 상수에 담기고, 그 아래 static_assert 가
// "레이아웃의 stride 가 정점 크기와 같다", "속성 5개" 를 컴파일 시점에 검사함 — 이전에는 런타임에
// 화면이 깨져야 알 수 있던 종류의 실수임.

struct VERTEX
{
	float x, y, z; 			// 좌표(Position)
	float r, g, b, a;		// 색상(Diffuse Color)
	float nx, ny, nz;		// 노멀(Normal)
	float u, v;				// 텍스처 좌표(TexCoord). (0,0) = 왼쪽 위 (D3D 관례)
	float tx, ty, tz, tw;	// 탄젠트(Tangent) = u 가 증가하는 방향. w = 손잡이(±1): B = cross(N, T) * w
};
using VertexPCNTT = VERTEX;

// 스키닝 정점 속성. 조인트 번호 4개(Skin::joints 의 인덱스, 최대 255)와 가중치 4개(합 1).
// Mesh 는 VERTEX 배열과 같은 길이의 이 배열을 따로 들고 있고, Renderer 가 GPU 에 올릴 때 VERTEX 뒤에 이어 붙임.
struct VertexSkin
{
	uint8_t joints[4] = { 0, 0, 0, 0 };
	float weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

// 스키닝 메시의 GPU 정점: VERTEX(64B) + VertexSkin(20B) = 84B.
struct VERTEX_SKINNED
{
	VERTEX base;
	VertexSkin skin;
};

enum class VertexFormat : uint8_t
{
	PositionColorNormalTexcoordTangent,       // VERTEX (VertexPCNTT)
	PositionColorNormalTexcoordTangentSkin,   // VERTEX_SKINNED. 위 포맷 뒤에 조인트 번호·가중치
};

// Position / Color / Normal / TexCoord / Tangent 레이아웃. 컴파일 시점 전용(consteval).
consteval VertexLayoutDesc MakeVertexLayoutPCNTT()
{
	VertexLayoutDesc layout;
	// C++20 지정 초기화. VertexAttribute 필드 순서: semantic, semanticIndex, format, offset, inputSlot.
	layout.attributes[0] = { .semantic = VertexSemantic::Position, .format = Format::R32G32B32_FLOAT,    .offset = static_cast<uint16_t>(offsetof(VERTEX, x)) };
	layout.attributes[1] = { .semantic = VertexSemantic::Color,    .format = Format::R32G32B32A32_FLOAT, .offset = static_cast<uint16_t>(offsetof(VERTEX, r)) };
	layout.attributes[2] = { .semantic = VertexSemantic::Normal,   .format = Format::R32G32B32_FLOAT,    .offset = static_cast<uint16_t>(offsetof(VERTEX, nx)) };
	layout.attributes[3] = { .semantic = VertexSemantic::TexCoord, .format = Format::R32G32_FLOAT,       .offset = static_cast<uint16_t>(offsetof(VERTEX, u)) };
	layout.attributes[4] = { .semantic = VertexSemantic::Tangent,  .format = Format::R32G32B32A32_FLOAT, .offset = static_cast<uint16_t>(offsetof(VERTEX, tx)) };
	layout.attributeCount = 5;
	layout.stride = sizeof(VERTEX);
	return layout;
}

inline constexpr VertexLayoutDesc kVertexLayoutPCNTT = MakeVertexLayoutPCNTT();

// 스키닝 포맷: PCNTT 속성 5개 + BLENDINDICES(R8G8B8A8_UINT) + BLENDWEIGHT(R32G32B32A32_FLOAT).
consteval VertexLayoutDesc MakeVertexLayoutPCNTTSkin()
{
	VertexLayoutDesc layout = MakeVertexLayoutPCNTT();
	layout.attributes[5] = { .semantic = VertexSemantic::BlendIndices, .format = Format::R8G8B8A8_UINT,     .offset = static_cast<uint16_t>(offsetof(VERTEX_SKINNED, skin.joints)) };
	layout.attributes[6] = { .semantic = VertexSemantic::BlendWeights, .format = Format::R32G32B32A32_FLOAT, .offset = static_cast<uint16_t>(offsetof(VERTEX_SKINNED, skin.weights)) };
	layout.attributeCount = 7;
	layout.stride = sizeof(VERTEX_SKINNED);
	return layout;
}

inline constexpr VertexLayoutDesc kVertexLayoutPCNTTSkin = MakeVertexLayoutPCNTTSkin();
static_assert(kVertexLayoutPCNTTSkin.stride == sizeof(VERTEX) + sizeof(VertexSkin), "스키닝 정점은 VERTEX 바로 뒤에 VertexSkin 이 붙음");

static_assert(kVertexLayoutPCNTT.stride == sizeof(VERTEX), "VertexPCNTT 레이아웃의 stride 가 정점 구조체 크기와 다름");
static_assert(kVertexLayoutPCNTT.attributeCount == 5, "VertexPCNTT 는 속성 5개(Position/Color/Normal/TexCoord/Tangent)");
static_assert(kVertexLayoutPCNTT.attributes[4].offset + sizeof(float) * 4 == sizeof(VERTEX), "마지막 속성(Tangent) 뒤에 빈 공간이 없어야 함");

// 이전 이름을 유지함 — 호출처는 그대로임. 이제 상수를 돌려줄 뿐임.
constexpr const VertexLayoutDesc& GetVertexLayoutPCNTT()
{
	return kVertexLayoutPCNTT;
}

constexpr const VertexLayoutDesc& GetVertexLayout(VertexFormat format)
{
	switch (format)
	{
	case VertexFormat::PositionColorNormalTexcoordTangentSkin:
		return kVertexLayoutPCNTTSkin;
	case VertexFormat::PositionColorNormalTexcoordTangent:
	default:
		return kVertexLayoutPCNTT;
	}
}

constexpr uint32_t GetVertexStride(VertexFormat format)
{
	return GetVertexLayout(format).stride;
}

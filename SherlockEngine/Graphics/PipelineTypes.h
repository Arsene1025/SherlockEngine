#pragma once
#include <cstdint>
#include <cstddef>
#include "Graphics/Handle.h"

// API 중립 파이프라인 상태 기술.
//
// 이 헤더는 D3D 헤더를 include하지 않는다. 필드 구성은 D3D12의
// D3D12_GRAPHICS_PIPELINE_STATE_DESC를 따른다. D3D11에서 쓰지 않는 항목
// (rtvFormats, dsvFormat, sampleCount)도 지금부터 넣어 둔다. D3D12 PSO는
// 이것 없이는 만들 수 없고, 나중에 추가하면 호출 코드가 전부 바뀐다.
//
// 모든 열거형은 uint8_t다. Desc를 해시 키로 쓰므로 크기가 작을수록 좋다.

enum class Format : uint8_t
{
	Unknown = 0,
	R8G8B8A8_UNORM,
	R8G8B8A8_UNORM_SRGB,
	D24_UNORM_S8_UINT,
	D32_FLOAT,
	R32_FLOAT,
	R32G32_FLOAT,
	R32G32B32_FLOAT,
	R32G32B32A32_FLOAT,
	R16_UINT,
	R32_UINT,
};

enum class FillMode : uint8_t { Solid, Wireframe };
enum class CullMode : uint8_t { None, Front, Back };
enum class CompareFunc : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class PrimitiveTopology : uint8_t { TriangleList, TriangleStrip, LineList, PointList };
enum class BlendFactor : uint8_t { Zero, One, SrcAlpha, InvSrcAlpha, SrcColor, InvSrcColor, DstAlpha, InvDstAlpha };
enum class BlendOp : uint8_t { Add, Subtract, RevSubtract, Min, Max };
enum class VertexSemantic : uint8_t { Position, Normal, Color, TexCoord, Tangent };
enum class ShaderStage : uint8_t { Vertex, Pixel };

// 기본값은 "불투명, 깊이 테스트 켬, 뒷면 컬링, 솔리드". PipelineStateDesc d{};
// 로 만들고 셰이더·정점 레이아웃만 채우면 일반적인 3D 드로우가 된다.
struct RasterizerDesc
{
	FillMode fill = FillMode::Solid;
	CullMode cull = CullMode::Back;
	bool frontCounterClockwise = false;   // false = 시계방향이 앞면 (Luna GeometryGenerator 관례)
	bool depthClipEnable = true;
	bool scissorEnable = false;
	bool multisampleEnable = false;
	bool antialiasedLineEnable = false;
	int32_t depthBias = 0;
	float depthBiasClamp = 0.0f;
	float slopeScaledDepthBias = 0.0f;
};

struct DepthStencilDesc
{
	bool depthEnable = true;
	bool depthWrite = true;
	CompareFunc depthFunc = CompareFunc::Less;
	bool stencilEnable = false;
	uint8_t stencilReadMask = 0xFF;
	uint8_t stencilWriteMask = 0xFF;
	// 스텐실 연산(StencilOp)은 쓰는 단계가 오면 추가한다.
};

struct RenderTargetBlendDesc
{
	bool blendEnable = false;
	BlendFactor srcColor = BlendFactor::One;
	BlendFactor dstColor = BlendFactor::Zero;
	BlendOp colorOp = BlendOp::Add;
	BlendFactor srcAlpha = BlendFactor::One;
	BlendFactor dstAlpha = BlendFactor::Zero;
	BlendOp alphaOp = BlendOp::Add;
	uint8_t writeMask = 0xF;   // RGBA
};

constexpr uint32_t kMaxRenderTargets = 8;
constexpr uint32_t kMaxVertexAttributes = 8;

struct BlendDesc
{
	bool alphaToCoverage = false;
	bool independentBlend = false;
	RenderTargetBlendDesc rt[kMaxRenderTargets];
};

struct VertexAttribute
{
	VertexSemantic semantic = VertexSemantic::Position;
	uint8_t semanticIndex = 0;
	Format format = Format::Unknown;
	uint16_t offset = 0;      // 정점 구조체 안의 바이트 오프셋 (offsetof)
	uint8_t inputSlot = 0;
};

struct VertexLayoutDesc
{
	VertexAttribute attributes[kMaxVertexAttributes];
	uint8_t attributeCount = 0;
	uint16_t stride = 0;
};

struct PipelineStateDesc
{
	ShaderHandle vs;
	ShaderHandle ps;
	VertexLayoutDesc vertexLayout;
	RasterizerDesc rasterizer;
	DepthStencilDesc depthStencil;
	BlendDesc blend;
	PrimitiveTopology topology = PrimitiveTopology::TriangleList;

	// D3D11은 이 세 항목을 PSO 생성에 쓰지 않는다. D3D12는 필수다.
	Format rtvFormats[kMaxRenderTargets] = { Format::R8G8B8A8_UNORM };
	uint8_t rtvCount = 1;
	Format dsvFormat = Format::D24_UNORM_S8_UINT;
	uint8_t sampleCount = 1;

	// 필드 단위 해시·비교. 구조체 통째로 memcmp/memcpy하면 패딩 바이트가
	// 섞여 같은 내용이 다른 키가 된다. PipelineTypes.cpp 참고.
	uint64_t Hash() const;
	bool operator==(const PipelineStateDesc& other) const;
	bool operator!=(const PipelineStateDesc& other) const { return !(*this == other); }
};

struct PipelineStateDescHasher
{
	size_t operator()(const PipelineStateDesc& desc) const { return static_cast<size_t>(desc.Hash()); }
};

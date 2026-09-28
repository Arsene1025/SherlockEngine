#pragma once
#include <cstdint>
#include <cstddef>
#include "RHI/Handle.h"

// API 중립 파이프라인 상태 기술.
//
// 이 헤더는 D3D 헤더를 include하지 않음. 필드 구성은 D3D12의
// D3D12_GRAPHICS_PIPELINE_STATE_DESC를 따름. D3D11에서 쓰지 않는 항목
// (rtvFormats, dsvFormat, sampleCount)도 지금부터 넣어 둠. D3D12 PSO는
// 이 항목 없이는 만들 수 없고, 나중에 추가하면 호출 코드를 전부 바꿔야 함.
//
// 모든 열거형은 uint8_t임. Desc를 해시 키로 쓰므로 크기가 작을수록 좋음.
//
// C++20 (2026-09-28): 하위 Desc(Rasterizer/DepthStencil/RenderTargetBlend/VertexAttribute)의
// operator== 는 `= default` 로 생성함. 멤버를 하나씩 나열한 비교 함수를 PipelineTypes.cpp 에
// 손으로 쓰던 것을 대체함 — 필드를 추가하면 비교에서 빠뜨리는 실수가 없어짐.
// PipelineStateDesc 자체는 default 로 만들 수 없음: 배열을 "쓰는 만큼(rtvCount 등)"만 비교해야 하기 때문.
// != 는 C++20 재작성 규칙으로 == 에서 자동으로 만들어지므로 따로 선언하지 않음.
//
// 호출 코드는 C++20 지정 초기화(designated initializer)로 채움:
//   TextureDesc d{ .width = 2048, .height = 2048, .format = Format::D32_FLOAT, .bindFlags = ... };
// 지정 초기화는 선언 순서를 따라야 하므로, 이 헤더의 필드 순서를 바꾸면 호출 코드가 컴파일되지 않음.
// 그것이 의도임 — D3D12 desc 처럼 "필드 순서가 계약"이 됨.

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
// 로 만들고 셰이더·정점 레이아웃만 채우면 일반적인 3D 드로우 설정이 됨.
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

	// float 멤버는 값 비교임: -0.0f == 0.0f (해시도 -0.0f 를 0.0f 로 정규화하므로 일관됨).
	// NaN 은 자기 자신과도 다르지만 깊이 바이어스에 NaN 을 넣는 경우는 없음.
	bool operator==(const RasterizerDesc&) const = default;
};

struct DepthStencilDesc
{
	bool depthEnable = true;
	bool depthWrite = true;
	CompareFunc depthFunc = CompareFunc::Less;
	bool stencilEnable = false;
	uint8_t stencilReadMask = 0xFF;
	uint8_t stencilWriteMask = 0xFF;
	// 스텐실 연산(StencilOp)은 이를 쓰는 단계가 오면 추가함.

	bool operator==(const DepthStencilDesc&) const = default;
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

	bool operator==(const RenderTargetBlendDesc&) const = default;
};

constexpr uint32_t kMaxRenderTargets = 8;
constexpr uint32_t kMaxVertexAttributes = 8;
constexpr uint32_t kMaxBindingSets = 4;   // 파이프라인 하나가 쓰는 BindingLayout(=ResourceSet) 수 상한

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

	bool operator==(const VertexAttribute&) const = default;
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
	// D3D12의 pRootSignature 자리. 갱신 빈도별 BindingLayout(프레임/오브젝트/재질)을
	// 순서대로 나열한 것이 루트 시그니처 하나에 해당함. D3D11 PSO 생성에는 쓰이지
	// 않지만 "이 파이프라인이 어떤 슬롯을 쓰는가"는 파이프라인의 속성이므로 Desc(해시 키)에 넣음.
	BindingLayoutHandle bindingLayouts[kMaxBindingSets];
	uint8_t bindingLayoutCount = 0;
	VertexLayoutDesc vertexLayout;
	RasterizerDesc rasterizer;
	DepthStencilDesc depthStencil;
	BlendDesc blend;
	PrimitiveTopology topology = PrimitiveTopology::TriangleList;

	// D3D11은 이 세 항목을 PSO 생성에 쓰지 않음. D3D12에서는 필수임.
	Format rtvFormats[kMaxRenderTargets] = { Format::R8G8B8A8_UNORM };
	uint8_t rtvCount = 1;
	Format dsvFormat = Format::D24_UNORM_S8_UINT;
	uint8_t sampleCount = 1;

	// 필드 단위 해시·비교. 구조체를 통째로 memcmp/memcpy하면 패딩 바이트가
	// 섞여 같은 내용이 다른 키가 됨. PipelineTypes.cpp 참고.
	// operator!= 는 선언하지 않음 — C++20 이 == 에서 재작성함.
	uint64_t Hash() const;
	bool operator==(const PipelineStateDesc& other) const;
};

struct PipelineStateDescHasher
{
	size_t operator()(const PipelineStateDesc& desc) const { return static_cast<size_t>(desc.Hash()); }
};

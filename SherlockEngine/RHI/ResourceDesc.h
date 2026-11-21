#pragma once
#include <cstdint>
#include <cstddef>
#include <span>                  // C++20: ShaderDesc::bytecode
#include "RHI/PipelineTypes.h"   // Format, ShaderStage, CompareFunc

// GPU 리소스 기술(Desc). D3D 헤더를 include하지 않음.
//
// 모든 리소스 생성은 "Desc를 넘기면 핸들이 돌아온다"는 한 가지 형태를 따름.
// 예전에는 CreateConstBuffer / CreateVertexBuffer / CreateIndexBuffer 가
// 각각 D3D 버퍼 스마트 포인터를 돌려줬음. 세 함수의 차이는 BindFlags 하나뿐이었음.
//
// 이름과 필드를 백엔드(D3D11·D3D12)와 무관하게 지었으므로 호출 코드는 백엔드에 상관없이 같음.

enum class BufferUsage : uint8_t
{
	Default,   // GPU 읽기/쓰기. CPU 갱신은 UpdateSubresource(전체 덮어쓰기).
	Dynamic,   // CPU가 매 프레임 씀. Map(WRITE_DISCARD). 프레임 수만큼 내부 복제.
	Staging,   // CPU 읽기용 사본. CommandList::CopyBuffer 로 채우고 Device::ReadStagingBuffer 로 읽음 (GPU 클러스터 비닝 통계)
};

// 비트 플래그. 한 버퍼가 Vertex | ShaderResource 처럼 두 역할을 겸할 수 있음.
enum BufferBind : uint8_t
{
	BufferBind_Vertex = 1 << 0,
	BufferBind_Index = 1 << 1,
	BufferBind_Constant = 1 << 2,
	BufferBind_ShaderResource = 1 << 3,   // StructuredBuffer (stride 필요). Raw 와 함께면 ByteAddressBuffer (stride 0).
	// 컴퓨트 셰이더가 RWStructuredBuffer 로 씀 (u#). Default usage + stride > 0 만 가능 (Raw 와 함께면 RWByteAddressBuffer,
	// stride 0). 두 백엔드의 CreateBuffer 가 같은 규칙(ValidateBufferDesc)으로 거부함. 상태(ResourceState)를 버퍼마다
	// 추적하므로 쓰기 전 Barrier(buf, UnorderedAccess), 읽기 전 Barrier(buf, ShaderResource) 가 필요함.
	BufferBind_UnorderedAccess = 1 << 4,
	// ByteAddressBuffer / RWByteAddressBuffer 뷰 (BindingType::RawBufferResource / RawBufferUnorderedAccess). 구조체
	// 버퍼가 아님: stride 0, 크기는 4 의 배수, Default 전용. ShaderResource · UnorderedAccess · IndirectArgs 와만 겸함.
	// 따로 둔 이유: D3D11 은 간접 인자 버퍼(DRAWINDIRECT_ARGS)를 구조체 버퍼로 만들 수 없음 — GPU 가 인자를 쓰려면 raw UAV 여야 함.
	BufferBind_Raw = 1 << 5,
	// DrawInstancedIndirect / DispatchIndirect 의 인자 버퍼가 될 수 있음. Default 전용, 이 엔진에서는 BufferBind_Raw 필수
	// (정점·인덱스·상수 버퍼와 겸할 수 없음). 인자를 읽기 전 Barrier(buf, IndirectArgument) 가 필요함.
	BufferBind_IndirectArgs = 1 << 6,
};

struct BufferDesc
{
	uint32_t size = 0;   // 바이트 단위. 상수버퍼는 16의 배수로 올림 처리됨.
	BufferUsage usage = BufferUsage::Default;
	uint8_t bindFlags = 0;             // BufferBind_* 조합
	uint32_t stride = 0;               // 정점 버퍼: 정점 크기. 구조화 버퍼: 원소 크기.
	const char* debugName = nullptr;   // Debug Layer 메시지에 나올 이름. 생성 시에만 읽음.
};

// 버퍼 Desc 의 usage·bindFlags·stride 조합 검사. 맞으면 nullptr, 어긋나면 이유. 두 백엔드의 CreateBuffer 가 같은
// 규칙으로 거부하도록 여기 둠 (텍스처의 CanBeUnorderedAccess 와 같은 이유). Dynamic 은 GPU 가 쓸 수 없고 D3D12 에서는
// 리소스가 없는 링 조각이라 UAV·Raw·IndirectArgs 가 될 수 없음.
constexpr const char* ValidateBufferDesc(const BufferDesc& desc)
{
	const bool raw = (desc.bindFlags & BufferBind_Raw) != 0;
	if ((desc.bindFlags & BufferBind_IndirectArgs) && !raw) return "IndirectArgs 버퍼는 BufferBind_Raw 가 필요함";
	if (raw)
	{
		if (desc.usage != BufferUsage::Default) return "Raw 버퍼는 Default usage 만 가능";
		if (desc.stride != 0) return "Raw 버퍼는 stride 가 0 이어야 함 (구조체 버퍼를 겸할 수 없음)";
		if (desc.size % 4 != 0) return "Raw 버퍼 크기는 4 의 배수여야 함";
		if (desc.bindFlags & (BufferBind_Vertex | BufferBind_Index | BufferBind_Constant)) return "Raw·IndirectArgs 버퍼는 정점·인덱스·상수 버퍼를 겸할 수 없음";
	}
	if (desc.bindFlags & BufferBind_UnorderedAccess)
	{
		if (desc.usage != BufferUsage::Default) return "UAV 버퍼는 Default usage 만 가능";
		if (!raw && desc.stride == 0) return "구조체 UAV 버퍼는 stride > 0 이 필요함";
	}
	return nullptr;
}

// 상태를 Barrier 로 명시해야 하는 버퍼 (UAV 또는 간접 인자). 그 밖의 Default 버퍼는 쓰기가 없어 Common 그대로 읽힘
// (D3D12 의 암묵 승격). SRV 로 읽기 전 ShaderResource 상태인지 검사하는 대상도 이것임.
constexpr bool IsStateTrackedBuffer(const BufferDesc& desc)
{
	return (desc.bindFlags & (BufferBind_UnorderedAccess | BufferBind_IndirectArgs)) != 0;
}

enum TextureBind : uint8_t
{
	TextureBind_RenderTarget = 1 << 0,
	TextureBind_DepthStencil = 1 << 1,
	TextureBind_ShaderResource = 1 << 2,
	// 컴퓨트 셰이더가 RWTexture2D 로 씀 (u#). 생성할 때 정해야 하고 깊이·MSAA·sRGB 포맷과는 함께 쓸 수 없음 —
	// 두 백엔드의 CreateTexture 가 같은 규칙으로 거부함. cs_5_0 에서 float4 같은 다성분 UAV 는 쓰기만 되므로
	// 읽기는 같은 텍스처의 SRV 로 함 (한 Dispatch 에서 같은 텍스처를 읽고 쓰지 않음).
	TextureBind_UnorderedAccess = 1 << 3,
};

// 큐브맵. 큐브는 "면 6장짜리 2D 텍스처 배열 + 큐브로 읽는 뷰"임 — D3D11 은 ArraySize 6 + MISC_TEXTURECUBE,
// D3D12 는 DepthOrArraySize 6 + SRV_DIMENSION_TEXTURECUBE. 면 순서는 D3D 관례 +X, −X, +Y, −Y, +Z, −Z (DDS
// 파일 순서와 같음). 지금 큐브는 ShaderResource 전용임 (렌더 타깃으로 그리는 큐브 — 반사 프로브 — 는 면별 RTV
// 가 필요해 아직 없음).
enum class TextureDimension : uint8_t
{
	Texture2D,     // 배열 크기 1
	TextureCube,   // 배열 크기 6. width == height 여야 함
};

struct TextureDesc
{
	TextureDimension dimension = TextureDimension::Texture2D;   // D3D12_RESOURCE_DESC 처럼 첫 필드. 생략하면 2D
	uint32_t width = 0;
	uint32_t height = 0;
	Format format = Format::Unknown;   // sRGB 텍스처는 *_UNORM_SRGB 로 만듦. 샘플링할 때 하드웨어가 선형 값으로 변환함.
	uint32_t mipLevels = 1;
	uint8_t bindFlags = 0;   // TextureBind_* 조합
	uint32_t sampleCount = 1;
	const char* debugName = nullptr;
};

// 배열 크기(면 수). 큐브 = 6, 그 외 = 1. 백엔드의 ArraySize / DepthOrArraySize 와 초기 데이터 개수가 이 값을 씀.
constexpr uint32_t GetArraySize(const TextureDesc& desc)
{
	return desc.dimension == TextureDimension::TextureCube ? 6u : 1u;
}

// TextureBind_UnorderedAccess 를 받을 수 있는 Desc 인가. 두 백엔드의 CreateTexture 가 같은 규칙으로 쓰도록 여기 둠.
// 2D 만 (큐브 UAV 는 2D 배열 뷰가 필요해 아직 없음), 깊이·MSAA·sRGB·블록 압축은 UAV 가 될 수 없음.
// 포맷별 typed UAV 쓰기 지원은 장치마다 다르므로 백엔드가 따로 물어봄.
constexpr bool CanBeUnorderedAccess(const TextureDesc& desc)
{
	const bool srgb = desc.format == Format::R8G8B8A8_UNORM_SRGB;
	return desc.dimension == TextureDimension::Texture2D && !(desc.bindFlags & TextureBind_DepthStencil) &&
		desc.sampleCount == 1 && !srgb && !IsBlockCompressed(desc.format);
}

// 텍스처 초기 데이터. 서브리소스(면 × 밉)마다 하나. 밉맵은 CPU에서 만들어 함께 올림.
// 순서는 D3D 서브리소스 인덱스 = mip + face × mipLevels 와 같음: 면 0 의 밉 0..N−1, 면 1 의 밉 0..N−1, ...
// 2D 텍스처는 면이 하나뿐이라 "밉 레벨마다 하나"였던 이전 규칙과 같음.
// D3D12에서는 이 배열이 업로드 힙 → CopyTextureRegion 경로로 처리됨. 호출 코드는 같음.
struct TextureSubresource
{
	const void* data = nullptr;
	uint32_t rowPitch = 0;   // 한 행의 바이트 수
};

enum class SamplerFilter : uint8_t
{
	Point,         // 최근접. 밉맵도 최근접
	Linear,        // 3선형 (밉 사이도 보간)
	Anisotropic,   // 비등방. maxAnisotropy 사용
	Comparison,    // 그림자 비교 샘플러. PCF용 선형 + compareFunc
};

enum class SamplerAddress : uint8_t
{
	Wrap,
	Clamp,
	Mirror,
	Border,
};

struct SamplerDesc
{
	SamplerFilter filter = SamplerFilter::Linear;
	SamplerAddress addressU = SamplerAddress::Wrap;
	SamplerAddress addressV = SamplerAddress::Wrap;
	SamplerAddress addressW = SamplerAddress::Wrap;
	uint8_t maxAnisotropy = 16;
	CompareFunc compareFunc = CompareFunc::LessEqual;   // Comparison 필터에서만
	float minLod = 0.0f;
	float maxLod = 1000.0f;   // 밉맵을 끄려면 0
	float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	const char* debugName = nullptr;
};

struct ShaderDesc
{
	ShaderStage stage = ShaderStage::Vertex;
	// 컴파일된 바이트코드. 컴파일은 ShaderCompiler가 담당함.
	// C++20: (const void*, size_t) 쌍을 std::span 하나로 합침. std::vector<uint8_t> 를 그대로
	// 대입할 수 있고, 포인터와 길이가 따로 다니다 어긋나는 일이 없음. span 은 소유하지 않으므로
	// CreateShader 가 돌아오기 전까지만 원본이 살아 있으면 됨 (백엔드는 사본을 뜸).
	std::span<const uint8_t> bytecode;
	const char* debugName = nullptr;
};

// 상수버퍼를 어느 스테이지에 바인딩할지 지정함. D3D11은 스테이지마다 슬롯이 따로 있음.
enum ShaderStageMask : uint8_t
{
	ShaderStageMask_Vertex = 1 << 0,
	ShaderStageMask_Pixel = 1 << 1,
	// 컴퓨트 패스의 레이아웃. 그래픽스 스테이지와 한 슬롯에 섞지 않음 (CreateBindingLayout 이 거부) — D3D12 에서
	// 컴퓨트 루트 시그니처는 그래픽스와 따로 바인딩되므로 한 레이아웃이 둘 다에 걸치면 뜻이 모호해짐.
	ShaderStageMask_Compute = 1 << 2,
};

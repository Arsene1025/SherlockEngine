#pragma once
#include <cstdint>
#include <cstddef>
#include "Graphics/PipelineTypes.h"   // Format, ShaderStage

// GPU 리소스 기술(Desc). D3D 헤더를 include하지 않는다.
//
// 모든 리소스는 "Desc를 넘기면 핸들이 돌아온다"는 한 가지 모양이다.
// 예전에는 CreateConstBuffer / CreateVertexBuffer / CreateIndexBuffer 가
// 각각 D3D 버퍼 스마트 포인터를 돌려줬다. 세 함수의 차이는 BindFlags 하나였다.
//
// 이름과 필드는 RHI 최종형(7단계)과 같게 짓는다. 지금은 D3D11 전용 구현이지만
// 호출 코드는 그때 바뀌지 않는다.

enum class BufferUsage : uint8_t
{
	Default,   // GPU 읽기/쓰기. CPU 갱신은 UpdateSubresource(전체 덮어쓰기).
	Dynamic,   // CPU가 매 프레임 쓴다. Map(WRITE_DISCARD). 프레임 수만큼 내부 복제.
	Staging,   // CPU 읽기용 사본. 지금은 만들기만 하고 쓰는 곳이 없다.
};

// 비트 플래그. 한 버퍼가 Vertex | ShaderResource 처럼 두 역할을 겸할 수 있다.
enum BufferBind : uint8_t
{
	BufferBind_Vertex         = 1 << 0,
	BufferBind_Index          = 1 << 1,
	BufferBind_Constant       = 1 << 2,
	BufferBind_ShaderResource = 1 << 3,   // StructuredBuffer. stride 필요.
};

struct BufferDesc
{
	uint32_t size = 0;                      // 바이트. 상수버퍼는 16의 배수로 올림된다.
	BufferUsage usage = BufferUsage::Default;
	uint8_t bindFlags = 0;                  // BufferBind_* 조합
	uint32_t stride = 0;                    // 정점 버퍼: 정점 크기. 구조화 버퍼: 원소 크기.
	const char* debugName = nullptr;        // Debug Layer 메시지에 나올 이름. 생성 시에만 읽는다.
};

enum TextureBind : uint8_t
{
	TextureBind_RenderTarget   = 1 << 0,
	TextureBind_DepthStencil   = 1 << 1,
	TextureBind_ShaderResource = 1 << 2,
};

struct TextureDesc
{
	uint32_t width = 0;
	uint32_t height = 0;
	Format format = Format::Unknown;
	uint32_t mipLevels = 1;
	uint8_t bindFlags = 0;                  // TextureBind_* 조합
	uint32_t sampleCount = 1;
	const char* debugName = nullptr;
};

struct ShaderDesc
{
	ShaderStage stage = ShaderStage::Vertex;
	const void* bytecode = nullptr;         // 컴파일된 바이트코드. 컴파일은 ShaderCompiler가 한다.
	size_t bytecodeSize = 0;
	const char* debugName = nullptr;
};

// 상수버퍼를 어느 스테이지에 바인딩할지. D3D11은 스테이지마다 슬롯이 따로 있다.
enum ShaderStageMask : uint8_t
{
	ShaderStageMask_Vertex = 1 << 0,
	ShaderStageMask_Pixel  = 1 << 1,
};

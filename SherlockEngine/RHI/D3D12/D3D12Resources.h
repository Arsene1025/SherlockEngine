#pragma once
#include "RHI/D3D12/D3D12Common.h"
#include "RHI/D3D12/D3D12DescriptorHeap.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "RHI/RenderPassTypes.h"
#include "RHI/GraphicsConfig.h"
#include <string>
#include <vector>

// 풀에 들어가는 D3D12 리소스 항목. D3D12Device 와 D3D12CommandList 만 접근함.
//
// D3D11 항목과의 차이:
//   - 뷰는 ComPtr 이 아니라 디스크립터 힙의 인덱스임.
//   - Dynamic 버퍼는 자체 리소스가 없음. UpdateBuffer 가 프레임별 업로드 링에서 조각을 할당받아 그 GPU 주소를 기억함
//     (D3D11 에서는 Map(WRITE_DISCARD) 이 드라이버 안에서 하던 "이름 바꾸기"를 여기서는 엔진이 직접 함).
//   - 상태(state)는 검사용이 아니라 실제 ResourceBarrier 의 before 값으로 쓰임.

struct D3D12Buffer
{
	BufferDesc desc;
	std::string name;
	ComPtr<ID3D12Resource> resource;                 // Default / Staging. Dynamic 은 비어 있음.
	D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;        // Default: 고정. Dynamic: 마지막 UpdateBuffer 가 받은 링 조각의 주소
	uint64_t updatedFrame = UINT64_MAX;              // Dynamic: 마지막으로 UpdateBuffer 한 프레임 번호
};

struct D3D12Texture
{
	TextureDesc desc;
	std::string name;
	ComPtr<ID3D12Resource> resource;
	bool typeless = false;                           // 깊이 + SRV 용도면 리소스를 TYPELESS 로 만듦
	ResourceState state = ResourceState::Common;     // CommandList::Barrier 가 전이시킴 (D3D12 에서는 실제 배리어를 기록함)

	// 디스크립터 인덱스. 처음 요청될 때 만듦.
	uint32_t rtv = D3D12DescriptorHeap::kInvalid;        // RTV 힙 (desc.format 그대로)
	uint32_t rtvSrgb = D3D12DescriptorHeap::kInvalid;    // RTV 힙 (*_SRGB 뷰)
	uint32_t dsv = D3D12DescriptorHeap::kInvalid;        // DSV 힙
	uint32_t srv = D3D12DescriptorHeap::kInvalid;        // 스테이징 SRV 힙 (CPU 전용). ResourceSet 이 여기서 복사함
	uint32_t srvVisible = D3D12DescriptorHeap::kInvalid; // 셰이더 가시 힙의 영구 SRV (ImGui::Image 용)
};

struct D3D12Shader
{
	ShaderStage stage = ShaderStage::Vertex;
	std::string name;
	std::vector<uint8_t> bytecode;   // DXBC. PSO 생성 시 D3D12_SHADER_BYTECODE 로 넘김
};

struct D3D12Sampler
{
	SamplerDesc desc;
	std::string name;
	uint32_t staging = D3D12DescriptorHeap::kInvalid;   // 스테이징 샘플러 힙 (CPU 전용)
};

struct D3D12BindingLayout
{
	BindingLayoutDesc desc;
	std::string name;
};

// ResourceSet = 디스크립터 테이블 두 개(SRV, 샘플러)의 영구 범위 + CB 핸들 목록.
// 셋을 만들 때 디스크립터를 셰이더 가시 힙에 복사해 두고, 드로우 시에는 테이블 시작 GPU 핸들만 바인딩함.
struct D3D12ResourceSet
{
	ResourceSetDesc desc;
	std::string name;
	uint32_t srvTableStart = D3D12DescriptorHeap::kInvalid;
	uint32_t srvCount = 0;
	uint32_t samplerTableStart = D3D12DescriptorHeap::kInvalid;
	uint32_t samplerCount = 0;
};

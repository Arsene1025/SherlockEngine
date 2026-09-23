#pragma once
#include "RHI/D3D12/D3D12Common.h"
#include "RHI/PipelineTypes.h"
#include "RHI/ResourceDesc.h"
#include "RHI/RenderPassTypes.h"

// RHI 열거 → D3D12 값. D3D11Convert 와 같은 모양이며, 백엔드마다 매핑 테이블을 따로 갖는다.
namespace D3D12Convert
{
	DXGI_FORMAT ToDXGI(Format format);
	D3D12_FILL_MODE ToD3D12(FillMode fill);
	D3D12_CULL_MODE ToD3D12(CullMode cull);
	D3D12_COMPARISON_FUNC ToD3D12(CompareFunc func);
	D3D12_PRIMITIVE_TOPOLOGY ToD3D12(PrimitiveTopology topology);
	D3D12_PRIMITIVE_TOPOLOGY_TYPE ToTopologyType(PrimitiveTopology topology);
	D3D12_BLEND ToD3D12(BlendFactor factor);
	D3D12_BLEND_OP ToD3D12(BlendOp op);
	D3D12_FILTER ToD3D12(SamplerFilter filter);
	D3D12_TEXTURE_ADDRESS_MODE ToD3D12(SamplerAddress address);
	D3D12_RESOURCE_STATES ToD3D12(ResourceState state);
	const char* ToSemanticName(VertexSemantic semantic);

	D3D12_RASTERIZER_DESC ToD3D12(const RasterizerDesc& desc);
	D3D12_DEPTH_STENCIL_DESC ToD3D12(const DepthStencilDesc& desc);
	D3D12_BLEND_DESC ToD3D12(const BlendDesc& desc);

	// 깊이 포맷의 세 얼굴 (D3D11DepthFormat 과 같은 규칙)
	bool IsDepthFormat(DXGI_FORMAT format);
	DXGI_FORMAT DepthTypeless(DXGI_FORMAT format);
	DXGI_FORMAT DepthShaderView(DXGI_FORMAT format);
	DXGI_FORMAT ToSrgb(DXGI_FORMAT format);

	// d3dx12.h 의 CD3DX12_* 대신 쓰는 작은 헬퍼
	D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type);
	D3D12_RESOURCE_DESC BufferDesc(uint64_t size, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);
	D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
}

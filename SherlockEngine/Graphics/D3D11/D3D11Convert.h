#pragma once
#include "Graphics/D3D11/D3D11Common.h"
#include "Graphics/PipelineTypes.h"

// API 중립 열거형/Desc → D3D11 타입 변환. 이 폴더 안에서만 쓴다.
// 대응이 없는 값은 Log::Error를 남기고 안전한 기본값을 돌려준다.
namespace D3D11Convert
{
	DXGI_FORMAT ToDXGI(Format format);
	D3D11_FILL_MODE ToD3D11(FillMode fill);
	D3D11_CULL_MODE ToD3D11(CullMode cull);
	D3D11_COMPARISON_FUNC ToD3D11(CompareFunc func);
	D3D11_PRIMITIVE_TOPOLOGY ToD3D11(PrimitiveTopology topology);
	D3D11_BLEND ToD3D11(BlendFactor factor);
	D3D11_BLEND_OP ToD3D11(BlendOp op);
	const char* ToSemanticName(VertexSemantic semantic);

	D3D11_RASTERIZER_DESC ToD3D11(const RasterizerDesc& desc);
	D3D11_DEPTH_STENCIL_DESC ToD3D11(const DepthStencilDesc& desc);
	D3D11_BLEND_DESC ToD3D11(const BlendDesc& desc);
}

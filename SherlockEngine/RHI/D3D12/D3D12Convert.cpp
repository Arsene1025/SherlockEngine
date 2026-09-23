#include "pch.h"
#include "RHI/D3D12/D3D12Convert.h"
#include "Core/Log.h"

namespace D3D12Convert
{
	DXGI_FORMAT ToDXGI(Format format)
	{
		switch (format)
		{
		case Format::Unknown:             return DXGI_FORMAT_UNKNOWN;
		case Format::R8G8B8A8_UNORM:      return DXGI_FORMAT_R8G8B8A8_UNORM;
		case Format::R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case Format::D24_UNORM_S8_UINT:   return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case Format::D32_FLOAT:           return DXGI_FORMAT_D32_FLOAT;
		case Format::R32_FLOAT:           return DXGI_FORMAT_R32_FLOAT;
		case Format::R32G32_FLOAT:        return DXGI_FORMAT_R32G32_FLOAT;
		case Format::R32G32B32_FLOAT:     return DXGI_FORMAT_R32G32B32_FLOAT;
		case Format::R32G32B32A32_FLOAT:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case Format::R16_UINT:            return DXGI_FORMAT_R16_UINT;
		case Format::R32_UINT:            return DXGI_FORMAT_R32_UINT;
		default:
			Log::Error("D3D12Convert : 알 수 없는 Format %d", static_cast<int>(format));
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	D3D12_FILL_MODE ToD3D12(FillMode fill)
	{
		return fill == FillMode::Wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
	}

	D3D12_CULL_MODE ToD3D12(CullMode cull)
	{
		switch (cull)
		{
		case CullMode::None:  return D3D12_CULL_MODE_NONE;
		case CullMode::Front: return D3D12_CULL_MODE_FRONT;
		case CullMode::Back:  return D3D12_CULL_MODE_BACK;
		default:              return D3D12_CULL_MODE_BACK;
		}
	}

	D3D12_COMPARISON_FUNC ToD3D12(CompareFunc func)
	{
		switch (func)
		{
		case CompareFunc::Never:        return D3D12_COMPARISON_FUNC_NEVER;
		case CompareFunc::Less:         return D3D12_COMPARISON_FUNC_LESS;
		case CompareFunc::Equal:        return D3D12_COMPARISON_FUNC_EQUAL;
		case CompareFunc::LessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
		case CompareFunc::Greater:      return D3D12_COMPARISON_FUNC_GREATER;
		case CompareFunc::NotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
		case CompareFunc::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		case CompareFunc::Always:       return D3D12_COMPARISON_FUNC_ALWAYS;
		default:                        return D3D12_COMPARISON_FUNC_LESS;
		}
	}

	D3D12_PRIMITIVE_TOPOLOGY ToD3D12(PrimitiveTopology topology)
	{
		switch (topology)
		{
		case PrimitiveTopology::TriangleList:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		case PrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		case PrimitiveTopology::LineList:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		case PrimitiveTopology::PointList:     return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		default:                               return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		}
	}

	// D3D12 PSO 는 "종류"(점·선·삼각형)만 안다. 리스트/스트립은 커맨드 리스트의 IASetPrimitiveTopology 가 정한다.
	D3D12_PRIMITIVE_TOPOLOGY_TYPE ToTopologyType(PrimitiveTopology topology)
	{
		switch (topology)
		{
		case PrimitiveTopology::TriangleList:
		case PrimitiveTopology::TriangleStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		case PrimitiveTopology::LineList:      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case PrimitiveTopology::PointList:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		default:                               return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		}
	}

	D3D12_BLEND ToD3D12(BlendFactor factor)
	{
		switch (factor)
		{
		case BlendFactor::Zero:        return D3D12_BLEND_ZERO;
		case BlendFactor::One:         return D3D12_BLEND_ONE;
		case BlendFactor::SrcAlpha:    return D3D12_BLEND_SRC_ALPHA;
		case BlendFactor::InvSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
		case BlendFactor::SrcColor:    return D3D12_BLEND_SRC_COLOR;
		case BlendFactor::InvSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
		case BlendFactor::DstAlpha:    return D3D12_BLEND_DEST_ALPHA;
		case BlendFactor::InvDstAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
		default:                       return D3D12_BLEND_ONE;
		}
	}

	D3D12_BLEND_OP ToD3D12(BlendOp op)
	{
		switch (op)
		{
		case BlendOp::Add:         return D3D12_BLEND_OP_ADD;
		case BlendOp::Subtract:    return D3D12_BLEND_OP_SUBTRACT;
		case BlendOp::RevSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
		case BlendOp::Min:         return D3D12_BLEND_OP_MIN;
		case BlendOp::Max:         return D3D12_BLEND_OP_MAX;
		default:                   return D3D12_BLEND_OP_ADD;
		}
	}

	D3D12_FILTER ToD3D12(SamplerFilter filter)
	{
		switch (filter)
		{
		case SamplerFilter::Point:       return D3D12_FILTER_MIN_MAG_MIP_POINT;
		case SamplerFilter::Linear:      return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		case SamplerFilter::Anisotropic: return D3D12_FILTER_ANISOTROPIC;
		case SamplerFilter::Comparison:  return D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		default:                         return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		}
	}

	D3D12_TEXTURE_ADDRESS_MODE ToD3D12(SamplerAddress address)
	{
		switch (address)
		{
		case SamplerAddress::Wrap:   return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		case SamplerAddress::Clamp:  return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		case SamplerAddress::Mirror: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		case SamplerAddress::Border: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		default:                     return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		}
	}

	// 6단계의 ResourceState 는 D3D12_RESOURCE_STATES 의 부분집합으로 설계했다. 여기서 1:1 로 풀린다.
	D3D12_RESOURCE_STATES ToD3D12(ResourceState state)
	{
		switch (state)
		{
		case ResourceState::Common:         return D3D12_RESOURCE_STATE_COMMON;
		case ResourceState::RenderTarget:   return D3D12_RESOURCE_STATE_RENDER_TARGET;
		case ResourceState::DepthWrite:     return D3D12_RESOURCE_STATE_DEPTH_WRITE;
		case ResourceState::ShaderResource: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		case ResourceState::CopySource:     return D3D12_RESOURCE_STATE_COPY_SOURCE;
		case ResourceState::CopyDest:       return D3D12_RESOURCE_STATE_COPY_DEST;
		case ResourceState::Present:        return D3D12_RESOURCE_STATE_PRESENT;
		default:                            return D3D12_RESOURCE_STATE_COMMON;
		}
	}

	const char* ToSemanticName(VertexSemantic semantic)
	{
		switch (semantic)
		{
		case VertexSemantic::Position: return "POSITION";
		case VertexSemantic::Normal:   return "NORMAL";
		case VertexSemantic::Color:    return "COLOR";
		case VertexSemantic::TexCoord: return "TEXCOORD";
		case VertexSemantic::Tangent:  return "TANGENT";
		default:                       return "POSITION";
		}
	}

	D3D12_RASTERIZER_DESC ToD3D12(const RasterizerDesc& desc)
	{
		D3D12_RASTERIZER_DESC rd = {};
		rd.FillMode = ToD3D12(desc.fill);
		rd.CullMode = ToD3D12(desc.cull);
		rd.FrontCounterClockwise = desc.frontCounterClockwise;
		rd.DepthBias = desc.depthBias;
		rd.DepthBiasClamp = desc.depthBiasClamp;
		rd.SlopeScaledDepthBias = desc.slopeScaledDepthBias;
		rd.DepthClipEnable = desc.depthClipEnable;
		rd.MultisampleEnable = desc.multisampleEnable;
		rd.AntialiasedLineEnable = desc.antialiasedLineEnable;
		rd.ForcedSampleCount = 0;
		rd.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		return rd;
	}

	D3D12_DEPTH_STENCIL_DESC ToD3D12(const DepthStencilDesc& desc)
	{
		D3D12_DEPTH_STENCIL_DESC dd = {};
		dd.DepthEnable = desc.depthEnable;
		dd.DepthWriteMask = desc.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		dd.DepthFunc = ToD3D12(desc.depthFunc);
		dd.StencilEnable = desc.stencilEnable;
		dd.StencilReadMask = desc.stencilReadMask;
		dd.StencilWriteMask = desc.stencilWriteMask;
		const D3D12_DEPTH_STENCILOP_DESC defaultOp =
		{
			D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS
		};
		dd.FrontFace = defaultOp;
		dd.BackFace = defaultOp;
		return dd;
	}

	D3D12_BLEND_DESC ToD3D12(const BlendDesc& desc)
	{
		D3D12_BLEND_DESC bd = {};
		bd.AlphaToCoverageEnable = desc.alphaToCoverage;
		bd.IndependentBlendEnable = desc.independentBlend;
		for (uint32_t i = 0; i < kMaxRenderTargets; ++i)
		{
			const RenderTargetBlendDesc& src = desc.rt[i];
			D3D12_RENDER_TARGET_BLEND_DESC& dst = bd.RenderTarget[i];
			dst.BlendEnable = src.blendEnable;
			dst.LogicOpEnable = FALSE;
			dst.SrcBlend = ToD3D12(src.srcColor);
			dst.DestBlend = ToD3D12(src.dstColor);
			dst.BlendOp = ToD3D12(src.colorOp);
			dst.SrcBlendAlpha = ToD3D12(src.srcAlpha);
			dst.DestBlendAlpha = ToD3D12(src.dstAlpha);
			dst.BlendOpAlpha = ToD3D12(src.alphaOp);
			dst.LogicOp = D3D12_LOGIC_OP_NOOP;
			dst.RenderTargetWriteMask = src.writeMask;
		}
		return bd;
	}

	bool IsDepthFormat(DXGI_FORMAT f) { return f == DXGI_FORMAT_D32_FLOAT || f == DXGI_FORMAT_D24_UNORM_S8_UINT; }

	DXGI_FORMAT DepthTypeless(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_D32_FLOAT:         return DXGI_FORMAT_R32_TYPELESS;
		case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24G8_TYPELESS;
		default:                            return f;
		}
	}

	DXGI_FORMAT DepthShaderView(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_D32_FLOAT:         return DXGI_FORMAT_R32_FLOAT;
		case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		default:                            return f;
		}
	}

	DXGI_FORMAT ToSrgb(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		default:                         return format;
		}
	}

	D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
	{
		D3D12_HEAP_PROPERTIES hp = {};
		hp.Type = type;
		hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		hp.CreationNodeMask = 1;
		hp.VisibleNodeMask = 1;
		return hp;
	}

	D3D12_RESOURCE_DESC BufferDesc(uint64_t size, D3D12_RESOURCE_FLAGS flags)
	{
		D3D12_RESOURCE_DESC rd = {};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		rd.Alignment = 0;
		rd.Width = size;
		rd.Height = 1;
		rd.DepthOrArraySize = 1;
		rd.MipLevels = 1;
		rd.Format = DXGI_FORMAT_UNKNOWN;
		rd.SampleDesc.Count = 1;
		rd.SampleDesc.Quality = 0;
		rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		rd.Flags = flags;
		return rd;
	}

	D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = resource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		return barrier;
	}
}

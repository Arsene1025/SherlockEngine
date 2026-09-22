#include "pch.h"
#include "Graphics/PipelineTypes.h"
#include <cstring>

// 해시는 필드를 하나씩 섞는다.
//
// 왜 memcpy/memcmp가 아닌가.
// (1) 구조체에는 정렬 패딩이 있고 그 바이트 값은 정해져 있지 않다. 같은 내용의
//     Desc 두 개가 패딩 때문에 다른 해시를 내면 캐시가 계속 미스난다.
// (2) float는 -0.0f == 0.0f 이지만 비트가 다르다. 비트 패턴으로 해시하되
//     0.0f로 정규화해서 넣는다.
// (3) 배열은 "쓰는 만큼"만 섞는다. rtvCount 뒤의 슬롯과 attributeCount 뒤의
//     속성은 의미가 없으므로 값이 달라도 같은 PSO여야 한다.
namespace
{
	inline uint64_t HashCombine(uint64_t seed, uint64_t value)
	{
		// boost::hash_combine의 64비트판. 황금비 상수로 섞는다.
		return seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
	}

	inline uint64_t HashFloat(uint64_t seed, float f)
	{
		if (f == 0.0f) f = 0.0f;   // -0.0f를 +0.0f로
		uint32_t bits = 0;
		std::memcpy(&bits, &f, sizeof(bits));
		return HashCombine(seed, bits);
	}

	inline bool FloatEq(float a, float b)
	{
		// 캐시 키이므로 비트 동일성이 맞다. 0.0f/-0.0f만 같은 것으로 본다.
		if (a == 0.0f && b == 0.0f) return true;
		uint32_t ab = 0, bb = 0;
		std::memcpy(&ab, &a, sizeof(ab));
		std::memcpy(&bb, &b, sizeof(bb));
		return ab == bb;
	}

	uint64_t HashRasterizer(uint64_t h, const RasterizerDesc& r)
	{
		h = HashCombine(h, static_cast<uint8_t>(r.fill));
		h = HashCombine(h, static_cast<uint8_t>(r.cull));
		h = HashCombine(h, r.frontCounterClockwise);
		h = HashCombine(h, r.depthClipEnable);
		h = HashCombine(h, r.scissorEnable);
		h = HashCombine(h, r.multisampleEnable);
		h = HashCombine(h, r.antialiasedLineEnable);
		h = HashCombine(h, static_cast<uint32_t>(r.depthBias));
		h = HashFloat(h, r.depthBiasClamp);
		h = HashFloat(h, r.slopeScaledDepthBias);
		return h;
	}

	bool EqRasterizer(const RasterizerDesc& a, const RasterizerDesc& b)
	{
		return a.fill == b.fill && a.cull == b.cull
			&& a.frontCounterClockwise == b.frontCounterClockwise
			&& a.depthClipEnable == b.depthClipEnable
			&& a.scissorEnable == b.scissorEnable
			&& a.multisampleEnable == b.multisampleEnable
			&& a.antialiasedLineEnable == b.antialiasedLineEnable
			&& a.depthBias == b.depthBias
			&& FloatEq(a.depthBiasClamp, b.depthBiasClamp)
			&& FloatEq(a.slopeScaledDepthBias, b.slopeScaledDepthBias);
	}

	uint64_t HashDepthStencil(uint64_t h, const DepthStencilDesc& d)
	{
		h = HashCombine(h, d.depthEnable);
		h = HashCombine(h, d.depthWrite);
		h = HashCombine(h, static_cast<uint8_t>(d.depthFunc));
		h = HashCombine(h, d.stencilEnable);
		h = HashCombine(h, d.stencilReadMask);
		h = HashCombine(h, d.stencilWriteMask);
		return h;
	}

	bool EqDepthStencil(const DepthStencilDesc& a, const DepthStencilDesc& b)
	{
		return a.depthEnable == b.depthEnable && a.depthWrite == b.depthWrite
			&& a.depthFunc == b.depthFunc && a.stencilEnable == b.stencilEnable
			&& a.stencilReadMask == b.stencilReadMask && a.stencilWriteMask == b.stencilWriteMask;
	}

	uint64_t HashRtBlend(uint64_t h, const RenderTargetBlendDesc& b)
	{
		h = HashCombine(h, b.blendEnable);
		h = HashCombine(h, static_cast<uint8_t>(b.srcColor));
		h = HashCombine(h, static_cast<uint8_t>(b.dstColor));
		h = HashCombine(h, static_cast<uint8_t>(b.colorOp));
		h = HashCombine(h, static_cast<uint8_t>(b.srcAlpha));
		h = HashCombine(h, static_cast<uint8_t>(b.dstAlpha));
		h = HashCombine(h, static_cast<uint8_t>(b.alphaOp));
		h = HashCombine(h, b.writeMask);
		return h;
	}

	bool EqRtBlend(const RenderTargetBlendDesc& a, const RenderTargetBlendDesc& b)
	{
		return a.blendEnable == b.blendEnable && a.srcColor == b.srcColor && a.dstColor == b.dstColor
			&& a.colorOp == b.colorOp && a.srcAlpha == b.srcAlpha && a.dstAlpha == b.dstAlpha
			&& a.alphaOp == b.alphaOp && a.writeMask == b.writeMask;
	}

	// independentBlend가 꺼져 있으면 D3D는 rt[0]만 본다. 해시도 그렇게 한다.
	uint32_t BlendSlotCount(const BlendDesc& b, uint8_t rtvCount)
	{
		return b.independentBlend ? rtvCount : 1u;
	}

	bool EqVertexAttribute(const VertexAttribute& a, const VertexAttribute& b)
	{
		return a.semantic == b.semantic && a.semanticIndex == b.semanticIndex
			&& a.format == b.format && a.offset == b.offset && a.inputSlot == b.inputSlot;
	}
}

uint64_t PipelineStateDesc::Hash() const
{
	uint64_t h = 0xCBF29CE484222325ull;   // FNV offset basis를 시드로

	h = HashCombine(h, vs.index);
	h = HashCombine(h, vs.generation);
	h = HashCombine(h, ps.index);
	h = HashCombine(h, ps.generation);

	h = HashCombine(h, vertexLayout.attributeCount);
	h = HashCombine(h, vertexLayout.stride);
	for (uint32_t i = 0; i < vertexLayout.attributeCount && i < kMaxVertexAttributes; ++i)
	{
		const VertexAttribute& a = vertexLayout.attributes[i];
		h = HashCombine(h, static_cast<uint8_t>(a.semantic));
		h = HashCombine(h, a.semanticIndex);
		h = HashCombine(h, static_cast<uint8_t>(a.format));
		h = HashCombine(h, a.offset);
		h = HashCombine(h, a.inputSlot);
	}

	h = HashRasterizer(h, rasterizer);
	h = HashDepthStencil(h, depthStencil);

	h = HashCombine(h, blend.alphaToCoverage);
	h = HashCombine(h, blend.independentBlend);
	const uint32_t blendSlots = BlendSlotCount(blend, rtvCount);
	for (uint32_t i = 0; i < blendSlots && i < kMaxRenderTargets; ++i)
	{
		h = HashRtBlend(h, blend.rt[i]);
	}

	h = HashCombine(h, static_cast<uint8_t>(topology));

	h = HashCombine(h, rtvCount);
	for (uint32_t i = 0; i < rtvCount && i < kMaxRenderTargets; ++i)
	{
		h = HashCombine(h, static_cast<uint8_t>(rtvFormats[i]));
	}
	h = HashCombine(h, static_cast<uint8_t>(dsvFormat));
	h = HashCombine(h, sampleCount);

	return h;
}

bool PipelineStateDesc::operator==(const PipelineStateDesc& o) const
{
	if (vs != o.vs || ps != o.ps) return false;

	if (vertexLayout.attributeCount != o.vertexLayout.attributeCount) return false;
	if (vertexLayout.stride != o.vertexLayout.stride) return false;
	for (uint32_t i = 0; i < vertexLayout.attributeCount && i < kMaxVertexAttributes; ++i)
	{
		if (!EqVertexAttribute(vertexLayout.attributes[i], o.vertexLayout.attributes[i])) return false;
	}

	if (!EqRasterizer(rasterizer, o.rasterizer)) return false;
	if (!EqDepthStencil(depthStencil, o.depthStencil)) return false;

	if (blend.alphaToCoverage != o.blend.alphaToCoverage) return false;
	if (blend.independentBlend != o.blend.independentBlend) return false;
	if (rtvCount != o.rtvCount) return false;
	const uint32_t blendSlots = BlendSlotCount(blend, rtvCount);
	for (uint32_t i = 0; i < blendSlots && i < kMaxRenderTargets; ++i)
	{
		if (!EqRtBlend(blend.rt[i], o.blend.rt[i])) return false;
	}

	if (topology != o.topology) return false;
	for (uint32_t i = 0; i < rtvCount && i < kMaxRenderTargets; ++i)
	{
		if (rtvFormats[i] != o.rtvFormats[i]) return false;
	}
	return dsvFormat == o.dsvFormat && sampleCount == o.sampleCount;
}

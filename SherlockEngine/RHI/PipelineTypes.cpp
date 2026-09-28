#include "pch.h"
#include "RHI/PipelineTypes.h"
#include <cstring>

// 해시는 필드를 하나씩 섞음.
//
// memcpy/memcmp를 쓰지 않는 이유.
// (1) 구조체에는 정렬 패딩이 있고 그 바이트 값은 정해져 있지 않음. 같은 내용의
//     Desc 두 개가 패딩 때문에 다른 해시를 내면 캐시 미스가 계속 남.
// (2) float는 -0.0f == 0.0f 이지만 비트가 다름. 비트 패턴으로 해시하되
//     -0.0f는 0.0f로 정규화해서 넣음.
// (3) 배열은 "쓰는 만큼"만 섞음. rtvCount 뒤의 슬롯과 attributeCount 뒤의
//     속성은 의미가 없으므로 값이 달라도 같은 PSO로 봐야 함.
//
// C++20 (2026-09-28): 하위 Desc 의 비교 함수(EqRasterizer / EqDepthStencil / EqRtBlend /
// EqVertexAttribute / FloatEq)는 삭제하고 헤더의 `operator== = default` 를 씀. 해시 함수는
// 그대로임 — 표준에 "기본 해시"는 없고, (3)의 "쓰는 만큼만" 규칙은 어차피 손으로 써야 함.
// float 비교 의미가 바뀐 지점: 예전 FloatEq 는 비트 비교(±0 만 예외)였고, 지금은 값 비교임.
// 두 방식은 NaN 에서만 다르고(값 비교는 NaN != NaN), 해시와의 일관성(a == b ⇒ hash(a) == hash(b))은
// 값 비교에서도 유지됨 — ±0 은 해시가 정규화하고, 그 외 값이 같으면 비트도 같기 때문.
namespace
{
	inline uint64_t HashCombine(uint64_t seed, uint64_t value)
	{
		// boost::hash_combine의 64비트판. 황금비 상수로 섞음.
		return seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
	}

	inline uint64_t HashFloat(uint64_t seed, float f)
	{
		if (f == 0.0f) f = 0.0f;   // -0.0f를 +0.0f로
		uint32_t bits = 0;
		std::memcpy(&bits, &f, sizeof(bits));
		return HashCombine(seed, bits);
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

	// independentBlend가 꺼져 있으면 D3D는 rt[0]만 봄. 해시도 rt[0]만 반영함.
	uint32_t BlendSlotCount(const BlendDesc& b, uint8_t rtvCount)
	{
		return b.independentBlend ? rtvCount : 1u;
	}
}

uint64_t PipelineStateDesc::Hash() const
{
	uint64_t h = 0xCBF29CE484222325ull;   // FNV offset basis를 시드로

	h = HashCombine(h, vs.index);
	h = HashCombine(h, vs.generation);
	h = HashCombine(h, ps.index);
	h = HashCombine(h, ps.generation);
	h = HashCombine(h, bindingLayoutCount);
	for (uint32_t i = 0; i < bindingLayoutCount && i < kMaxBindingSets; ++i)
	{
		h = HashCombine(h, bindingLayouts[i].index);
		h = HashCombine(h, bindingLayouts[i].generation);
	}

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
	// Handle 의 != 는 default <=> 에서, 하위 Desc 의 == / != 는 default == 에서 재작성됨.
	if (vs != o.vs || ps != o.ps) return false;
	if (bindingLayoutCount != o.bindingLayoutCount) return false;
	for (uint32_t i = 0; i < bindingLayoutCount && i < kMaxBindingSets; ++i)
	{
		if (bindingLayouts[i] != o.bindingLayouts[i]) return false;
	}

	if (vertexLayout.attributeCount != o.vertexLayout.attributeCount) return false;
	if (vertexLayout.stride != o.vertexLayout.stride) return false;
	for (uint32_t i = 0; i < vertexLayout.attributeCount && i < kMaxVertexAttributes; ++i)
	{
		if (vertexLayout.attributes[i] != o.vertexLayout.attributes[i]) return false;
	}

	if (rasterizer != o.rasterizer) return false;
	if (depthStencil != o.depthStencil) return false;

	if (blend.alphaToCoverage != o.blend.alphaToCoverage) return false;
	if (blend.independentBlend != o.blend.independentBlend) return false;
	if (rtvCount != o.rtvCount) return false;
	const uint32_t blendSlots = BlendSlotCount(blend, rtvCount);
	for (uint32_t i = 0; i < blendSlots && i < kMaxRenderTargets; ++i)
	{
		if (blend.rt[i] != o.blend.rt[i]) return false;
	}

	if (topology != o.topology) return false;
	for (uint32_t i = 0; i < rtvCount && i < kMaxRenderTargets; ++i)
	{
		if (rtvFormats[i] != o.rtvFormats[i]) return false;
	}
	return dsvFormat == o.dsvFormat && sampleCount == o.sampleCount;
}

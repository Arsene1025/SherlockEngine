#include "pch.h"
#include "RHI/D3D11/PipelineStateCache.h"
#include "Core/Log.h"

PipelineHandle PipelineStateCache::GetOrCreate(D3D11Device& device, const PipelineStateDesc& desc)
{
	const auto found = m_lookup.find(desc);
	if (found != m_lookup.end())
	{
		return PipelineHandle{ found->second, m_generation };
	}

	auto state = std::make_unique<PipelineState>();
	if (!state->Create(device, desc))
	{
		Log::Error("PipelineStateCache : PSO 생성 실패.");
		return PipelineHandle{};
	}

	const uint32_t index = static_cast<uint32_t>(m_states.size());
	m_states.push_back(std::move(state));
	m_lookup.emplace(desc, index);
	Log::Info("PSO 생성 #%u (fill=%s, cull=%d, layouts=%u, gen=%u). 캐시 크기 %zu",
		index,
		desc.rasterizer.fill == FillMode::Wireframe ? "wire" : "solid",
		static_cast<int>(desc.rasterizer.cull),
		desc.bindingLayoutCount,
		m_generation,
		m_states.size());
	return PipelineHandle{ index, m_generation };
}

const PipelineState* PipelineStateCache::Get(PipelineHandle handle) const
{
	if (!handle.IsValid() || handle.index >= m_states.size() || handle.generation != m_generation)
	{
		return nullptr;
	}
	return m_states[handle.index].get();
}

void PipelineStateCache::Clear()
{
	if (!m_states.empty())
	{
		Log::Info("PSO 캐시 비움 (%zu개). 이전 세대 %u 의 핸들은 무효.", m_states.size(), m_generation);
	}
	m_lookup.clear();
	m_states.clear();
	++m_generation;
	if (m_generation == 0) m_generation = 1;
}

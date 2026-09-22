#include "pch.h"
#include "Graphics/D3D11/PipelineStateCache.h"
#include "Core/Log.h"

PipelineHandle PipelineStateCache::GetOrCreate(Device& device, const PipelineStateDesc& desc)
{
	const auto found = m_lookup.find(desc);
	if (found != m_lookup.end())
	{
		return PipelineHandle{ found->second, 1 };
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
	Log::Info("PSO 생성 #%u (fill=%s, cull=%d). 캐시 크기 %zu",
		index,
		desc.rasterizer.fill == FillMode::Wireframe ? "wire" : "solid",
		static_cast<int>(desc.rasterizer.cull),
		m_states.size());
	return PipelineHandle{ index, 1 };
}

const PipelineState* PipelineStateCache::Get(PipelineHandle handle) const
{
	if (!handle.IsValid() || handle.index >= m_states.size() || handle.generation != 1)
	{
		return nullptr;
	}
	return m_states[handle.index].get();
}

void PipelineStateCache::Clear()
{
	m_lookup.clear();
	m_states.clear();
}

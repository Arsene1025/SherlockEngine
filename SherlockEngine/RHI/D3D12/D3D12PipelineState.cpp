#include "pch.h"
#include "RHI/D3D12/D3D12PipelineState.h"
#include "RHI/D3D12/D3D12Device.h"
#include "RHI/D3D12/D3D12Convert.h"
#include "Core/Log.h"

bool D3D12PipelineState::Create(D3D12Device& device, const PipelineStateDesc& desc)
{
	m_desc = desc;
	ID3D12Device* d3d = device.GetDevice();
	if (d3d == nullptr)
	{
		Log::Error("D3D12PipelineState::Create : Device가 없음.");
		return false;
	}

	const D3D12Shader* vs = device.GetShader(desc.vs);
	if (vs == nullptr || vs->stage != ShaderStage::Vertex || vs->bytecode.empty())
	{
		Log::Error("D3D12PipelineState::Create : 정점 셰이더 핸들이 유효하지 않음.");
		return false;
	}
	const D3D12Shader* ps = nullptr;
	if (desc.ps.IsValid())
	{
		ps = device.GetShader(desc.ps);
		if (ps == nullptr || ps->stage != ShaderStage::Pixel || ps->bytecode.empty())
		{
			Log::Error("D3D12PipelineState::Create : 픽셀 셰이더 핸들이 유효하지 않음.");
			return false;
		}
	}

	// 루트 시그니처 — 레이아웃 조합이 같으면 공유
	m_rootLayout = device.GetOrCreateRootLayout(desc);
	if (!m_rootLayout)
	{
		Log::Error("D3D12PipelineState::Create : 루트 시그니처 생성 실패.");
		return false;
	}

	if (desc.vertexLayout.attributeCount == 0 || desc.vertexLayout.attributeCount > kMaxVertexAttributes)
	{
		Log::Error("D3D12PipelineState::Create : 정점 속성 개수가 잘못됨 (%u).", desc.vertexLayout.attributeCount);
		return false;
	}
	D3D12_INPUT_ELEMENT_DESC elements[kMaxVertexAttributes] = {};
	for (uint32_t i = 0; i < desc.vertexLayout.attributeCount; ++i)
	{
		const VertexAttribute& a = desc.vertexLayout.attributes[i];
		elements[i].SemanticName = D3D12Convert::ToSemanticName(a.semantic);
		elements[i].SemanticIndex = a.semanticIndex;
		elements[i].Format = D3D12Convert::ToDXGI(a.format);
		elements[i].InputSlot = a.inputSlot;
		elements[i].AlignedByteOffset = a.offset;
		elements[i].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
		elements[i].InstanceDataStepRate = 0;
	}

	// 1단계에서 "D3D11 은 안 쓰지만 D3D12 는 필수"라고 넣어 둔 rtvFormats / dsvFormat / sampleCount 가 여기서 쓰임.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
	pd.pRootSignature = m_rootLayout->rootSignature.Get();
	pd.VS = { vs->bytecode.data(), vs->bytecode.size() };
	if (ps) pd.PS = { ps->bytecode.data(), ps->bytecode.size() };   // 없으면 깊이 전용
	pd.BlendState = D3D12Convert::ToD3D12(desc.blend);
	pd.SampleMask = UINT_MAX;
	pd.RasterizerState = D3D12Convert::ToD3D12(desc.rasterizer);
	pd.DepthStencilState = D3D12Convert::ToD3D12(desc.depthStencil);
	pd.InputLayout = { elements, desc.vertexLayout.attributeCount };
	pd.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	pd.PrimitiveTopologyType = D3D12Convert::ToTopologyType(desc.topology);
	pd.NumRenderTargets = desc.rtvCount;
	for (uint32_t i = 0; i < desc.rtvCount && i < kMaxRenderTargets; ++i)
	{
		pd.RTVFormats[i] = D3D12Convert::ToDXGI(desc.rtvFormats[i]);
	}
	pd.DSVFormat = D3D12Convert::ToDXGI(desc.dsvFormat);
	pd.SampleDesc.Count = desc.sampleCount;
	pd.SampleDesc.Quality = 0;
	pd.NodeMask = 0;
	pd.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	const HRESULT hr = d3d->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_pso.ReleaseAndGetAddressOf()));
	if (FAILED(hr))
	{
		Log::Error("D3D12PipelineState::Create : CreateGraphicsPipelineState 실패. %s", Log::HrToString(hr).c_str());
		return false;
	}
	m_pso->SetName(L"PSO");
	m_topology = D3D12Convert::ToD3D12(desc.topology);
	return true;
}

// ------------------------------------------------------------------ 캐시

PipelineHandle D3D12PipelineStateCache::GetOrCreate(D3D12Device& device, const PipelineStateDesc& desc)
{
	const auto found = m_lookup.find(desc);
	if (found != m_lookup.end())
	{
		return PipelineHandle{ found->second, m_generation };
	}

	auto state = std::make_unique<D3D12PipelineState>();
	if (!state->Create(device, desc))
	{
		Log::Error("D3D12PipelineStateCache : PSO 생성 실패.");
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

const D3D12PipelineState* D3D12PipelineStateCache::Get(PipelineHandle handle) const
{
	if (!handle.IsValid() || handle.index >= m_states.size() || handle.generation != m_generation)
	{
		return nullptr;
	}
	return m_states[handle.index].get();
}

void D3D12PipelineStateCache::Clear(D3D12Device& device)
{
	if (!m_states.empty())
	{
		Log::Info("PSO 캐시 비움 (%zu개). 이전 세대 %u 의 핸들은 무효.", m_states.size(), m_generation);
	}
	// GPU 가 아직 이전 프레임에서 이 PSO 를 쓰고 있을 수 있음. 따라서 프레임 펜스를 통과한 뒤 해제함.
	for (auto& state : m_states)
	{
		device.DeferRelease(state->Detach());
	}
	m_lookup.clear();
	m_states.clear();
	++m_generation;
	if (m_generation == 0) m_generation = 1;
}

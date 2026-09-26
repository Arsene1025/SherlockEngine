#include "pch.h"
#include "RHI/D3D12/D3D12CommandList.h"
#include "RHI/D3D12/D3D12Device.h"
#include "RHI/D3D12/D3D12Convert.h"
#include "RHI/D3D12/D3D12PipelineState.h"
#include "Core/Log.h"

void D3D12CommandList::ErrorOnce(bool& flag, const char* message)
{
	if (flag) return;
	flag = true;
	Log::Error("%s", message);
}

void D3D12CommandList::Init(D3D12Device* device)
{
	m_device = device;
	m_list = nullptr;
	m_inPass = false;
	m_setCount = 0;
}

void D3D12CommandList::BeginFrame(ID3D12GraphicsCommandList* list)
{
	m_list = list;
	m_inPass = false;
	m_currentPipeline = PipelineHandle{};
	m_currentPso = nullptr;
	m_currentRoot = nullptr;
	// 기억해 둔 바인딩은 유지하되 (Renderer 가 프레임 초에 다시 설정함) 디스크립터 테이블은 다시 바인딩해야 함.
	for (uint32_t i = 0; i < m_setCount; ++i) m_sets[i].tablesDirty = true;
}

// ------------------------------------------------------------------ 렌더 패스

void D3D12CommandList::BeginRenderPass(const RenderPassDesc& desc)
{
	static bool warnedNested = false;
	if (m_list == nullptr) return;
	if (m_inPass)
	{
		ErrorOnce(warnedNested, "BeginRenderPass : 이전 패스가 끝나지 않음. EndRenderPass 를 먼저 부를 것.");
		EndRenderPass();
	}
	m_inPass = true;
	++m_stats.renderPasses;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[kMaxRenderTargets] = {};
	uint32_t rtvCount = 0;
	uint32_t width = 0, height = 0;
	for (uint32_t i = 0; i < desc.colorCount; ++i)
	{
		D3D12Texture* texture = m_device->GetTexture(desc.colors[i].texture);
		if (texture == nullptr)
		{
			Log::Error("BeginRenderPass : 컬러 attachment %u 의 텍스처 핸들이 유효하지 않음 (%s).", i, desc.debugName ? desc.debugName : "");
			continue;
		}
		rtvs[rtvCount++] = m_device->GetRTV(*texture, desc.colors[i].srgbView);
		if (width == 0) { width = texture->desc.width; height = texture->desc.height; }
	}
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
	bool hasDsv = false;
	if (desc.depth.texture.IsValid())
	{
		D3D12Texture* texture = m_device->GetTexture(desc.depth.texture);
		if (texture == nullptr)
		{
			Log::Error("BeginRenderPass : 깊이 attachment 의 텍스처 핸들이 유효하지 않음 (%s).", desc.debugName ? desc.debugName : "");
		}
		else
		{
			dsv = m_device->GetDSV(*texture);
			hasDsv = true;
			if (width == 0) { width = texture->desc.width; height = texture->desc.height; }
		}
	}

	m_list->OMSetRenderTargets(rtvCount, rtvCount ? rtvs : nullptr, FALSE, hasDsv ? &dsv : nullptr);

	for (uint32_t i = 0; i < rtvCount; ++i)
	{
		if (desc.colors[i].load == LoadOp::Clear)
		{
			m_list->ClearRenderTargetView(rtvs[i], desc.colors[i].clearColor, 0, nullptr);
		}
	}
	if (hasDsv && desc.depth.load == LoadOp::Clear)
	{
		m_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, desc.depth.clearDepth, desc.depth.clearStencil, 0, nullptr);
	}

	// 뷰포트 + 시저. 둘 다 PSO 밖의 동적 상태이고, 커맨드 리스트를 Reset 하면 사라짐.
	D3D12_VIEWPORT viewport = {};
	viewport.TopLeftX = desc.viewport.x;
	viewport.TopLeftY = desc.viewport.y;
	viewport.Width = desc.viewport.width > 0.0f ? desc.viewport.width : static_cast<float>(width);
	viewport.Height = desc.viewport.height > 0.0f ? desc.viewport.height : static_cast<float>(height);
	viewport.MinDepth = desc.viewport.minDepth;
	viewport.MaxDepth = desc.viewport.maxDepth;
	m_list->RSSetViewports(1, &viewport);

	D3D12_RECT scissor = {};
	scissor.left = static_cast<LONG>(viewport.TopLeftX);
	scissor.top = static_cast<LONG>(viewport.TopLeftY);
	scissor.right = static_cast<LONG>(viewport.TopLeftX + viewport.Width);
	scissor.bottom = static_cast<LONG>(viewport.TopLeftY + viewport.Height);
	m_list->RSSetScissorRects(1, &scissor);
}

void D3D12CommandList::EndRenderPass()
{
	// D3D12 의 OMSetRenderTargets 는 참조를 보유하지 않으므로 해제할 것이 없음. Store op 로 할 일도 없음.
	m_inPass = false;
}

// ------------------------------------------------------------------ 상태·바인딩

void D3D12CommandList::SetPipelineState(PipelineHandle handle)
{
	static bool warned = false;
	const D3D12PipelineState* pso = m_device ? m_device->GetPipeline(handle) : nullptr;
	if (pso == nullptr || m_list == nullptr)
	{
		ErrorOnce(warned, "SetPipelineState : 유효하지 않은 PipelineHandle.");
		return;
	}
	if (handle == m_currentPipeline) return;

	m_list->SetPipelineState(pso->Get());
	const D3D12RootLayout* root = pso->GetRootLayout();
	if (root != m_currentRoot)
	{
		// 루트 시그니처가 바뀌면 루트 파라미터가 전부 무효가 됨. 따라서 테이블을 다시 바인딩하게 함.
		m_list->SetGraphicsRootSignature(root->rootSignature.Get());
		m_currentRoot = root;
		for (uint32_t i = 0; i < m_setCount; ++i) m_sets[i].tablesDirty = true;
	}
	m_list->IASetPrimitiveTopology(pso->GetTopology());
	m_currentPipeline = handle;
	m_currentPso = pso;
}

void D3D12CommandList::SetResourceSet(ResourceSetHandle handle)
{
	static bool warned = false;
	const D3D12ResourceSet* set = m_device ? m_device->GetResourceSet(handle) : nullptr;
	if (set == nullptr)
	{
		ErrorOnce(warned, "SetResourceSet : 유효하지 않은 ResourceSetHandle.");
		return;
	}
	// 같은 레이아웃의 셋이 이미 있으면 교체, 없으면 추가. 실제 바인딩은 DrawIndexed 직전.
	for (uint32_t i = 0; i < m_setCount; ++i)
	{
		if (m_sets[i].layout == set->desc.layout)
		{
			if (m_sets[i].set != handle) { m_sets[i].set = handle; m_sets[i].tablesDirty = true; }
			return;
		}
	}
	if (m_setCount < kMaxBindingSets)
	{
		m_sets[m_setCount++] = BoundSet{ set->desc.layout, handle, true };
	}
}

void D3D12CommandList::SetVertexBuffer(BufferHandle handle, uint32_t slot, uint32_t offset)
{
	static bool warned = false;
	const D3D12Buffer* buffer = m_device ? m_device->GetBuffer(handle) : nullptr;
	if (buffer == nullptr || buffer->gpuAddress == 0 || m_list == nullptr)
	{
		ErrorOnce(warned, "SetVertexBuffer : 유효하지 않은 BufferHandle.");
		return;
	}
	D3D12_VERTEX_BUFFER_VIEW view = {};
	view.BufferLocation = buffer->gpuAddress + offset;
	view.SizeInBytes = buffer->desc.size - offset;
	view.StrideInBytes = buffer->desc.stride;
	m_list->IASetVertexBuffers(slot, 1, &view);
}

void D3D12CommandList::SetIndexBuffer(BufferHandle handle, Format indexFormat)
{
	static bool warned = false;
	const D3D12Buffer* buffer = m_device ? m_device->GetBuffer(handle) : nullptr;
	if (buffer == nullptr || buffer->gpuAddress == 0 || m_list == nullptr)
	{
		ErrorOnce(warned, "SetIndexBuffer : 유효하지 않은 BufferHandle.");
		return;
	}
	D3D12_INDEX_BUFFER_VIEW view = {};
	view.BufferLocation = buffer->gpuAddress;
	view.SizeInBytes = buffer->desc.size;
	view.Format = D3D12Convert::ToDXGI(indexFormat);
	m_list->IASetIndexBuffer(&view);
}

// 드로우 직전: 현재 루트 시그니처의 셋 순서대로 루트 CBV 와 테이블을 바인딩함.
// 루트 CBV 는 드로우마다 다시 바인딩함 — 오브젝트 상수버퍼가 드로우마다 UpdateBuffer 로 새 링 주소를 받기 때문임.
// 테이블은 셋이나 루트 시그니처가 바뀌었을 때만 다시 바인딩함.
void D3D12CommandList::ApplyBindings()
{
	static bool warnedMissing = false;
	static bool warnedStale = false;
	if (m_currentRoot == nullptr) return;

	for (uint32_t s = 0; s < m_currentRoot->setCount; ++s)
	{
		const D3D12RootLayout::SetBinding& binding = m_currentRoot->sets[s];
		BoundSet* bound = nullptr;
		for (uint32_t i = 0; i < m_setCount; ++i)
		{
			if (m_sets[i].layout == binding.layout) { bound = &m_sets[i]; break; }
		}
		if (bound == nullptr)
		{
			ErrorOnce(warnedMissing, "DrawIndexed : PSO 가 요구하는 BindingLayout 에 ResourceSet 이 바인딩되지 않음.");
			continue;
		}
		const D3D12ResourceSet* set = m_device->GetResourceSet(bound->set);
		const D3D12BindingLayout* layout = m_device->GetBindingLayout(binding.layout);
		if (set == nullptr || layout == nullptr) continue;

		for (uint32_t i = 0; i < layout->desc.slotCount; ++i)
		{
			if (layout->desc.slots[i].type != BindingType::ConstantBuffer) continue;
			const uint32_t root = binding.cbvRoot[i];
			if (root == UINT32_MAX) continue;
			const D3D12Buffer* buffer = m_device->GetBuffer(set->desc.bindings[i].buffer);
			if (buffer == nullptr || buffer->gpuAddress == 0)
			{
				ErrorOnce(warnedStale, "DrawIndexed : 상수버퍼가 아직 UpdateBuffer 되지 않음 (Dynamic 은 프레임마다 써야 한다).");
				continue;
			}
			if (buffer->desc.usage == BufferUsage::Dynamic && buffer->updatedFrame != m_device->GetFrameNumber())
			{
				ErrorOnce(warnedStale, "DrawIndexed : Dynamic 상수버퍼가 이번 프레임에 갱신되지 않음. 이전 프레임의 링 조각은 재사용될 수 있다.");
			}
			m_list->SetGraphicsRootConstantBufferView(root, buffer->gpuAddress);
		}
		if (bound->tablesDirty)
		{
			if (binding.srvTableRoot != UINT32_MAX && set->srvCount > 0)
			{
				m_list->SetGraphicsRootDescriptorTable(binding.srvTableRoot, m_device->GetSrvHeap().Gpu(set->srvTableStart));
			}
			if (binding.samplerTableRoot != UINT32_MAX && set->samplerCount > 0)
			{
				m_list->SetGraphicsRootDescriptorTable(binding.samplerTableRoot, m_device->GetSamplerHeap().Gpu(set->samplerTableStart));
			}
			bound->tablesDirty = false;
		}
	}
}

void D3D12CommandList::DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
{
	static bool warnedOutside = false;
	if (!m_inPass)
	{
		ErrorOnce(warnedOutside, "DrawIndexed : 렌더 패스 밖에서 드로우. BeginRenderPass 가 필요하다.");
		return;
	}
	if (m_list == nullptr || m_currentPso == nullptr) return;
	ApplyBindings();
	m_list->DrawIndexedInstanced(indexCount, 1, startIndex, baseVertex, 0);
}

// ------------------------------------------------------------------ 전이·복사

void D3D12CommandList::Barrier(TextureHandle handle, ResourceState before, ResourceState after)
{
	++m_stats.barriers;
	D3D12Texture* texture = m_device ? m_device->GetTexture(handle) : nullptr;
	if (texture == nullptr || m_list == nullptr)
	{
		static bool warned = false;
		ErrorOnce(warned, "Barrier : 유효하지 않은 TextureHandle.");
		return;
	}
	// 호출 코드가 넘긴 before 가 추적 상태와 다르면 D3D12 에서는 에러가 남. 따라서 추적 상태를 신뢰하고 경고만 남김.
	ResourceState actualBefore = texture->state;
	if (actualBefore != before)
	{
		Log::Warn("Barrier : '%s' 의 현재 상태는 %s 인데 before=%s 로 호출됨 (after=%s). 추적 상태를 씀.",
			texture->name.c_str(), ToString(actualBefore), ToString(before), ToString(after));
	}
	if (actualBefore == after) return;   // 전이 없음

	const D3D12_RESOURCE_BARRIER barrier = D3D12Convert::TransitionBarrier(
		texture->resource.Get(), D3D12Convert::ToD3D12(actualBefore), D3D12Convert::ToD3D12(after));
	m_list->ResourceBarrier(1, &barrier);
	texture->state = after;
}

void D3D12CommandList::CopyBuffer(BufferHandle dst, BufferHandle src)
{
	const D3D12Buffer* d = m_device ? m_device->GetBuffer(dst) : nullptr;
	const D3D12Buffer* s = m_device ? m_device->GetBuffer(src) : nullptr;
	if (d == nullptr || s == nullptr || !d->resource || !s->resource || m_list == nullptr)
	{
		Log::Error("CopyBuffer : 유효하지 않은 BufferHandle (Dynamic 버퍼는 복사 대상이 될 수 없음).");
		return;
	}
	if (d->desc.size != s->desc.size)
	{
		Log::Error("CopyBuffer : 크기가 다름 (%s %u, %s %u).", d->name.c_str(), d->desc.size, s->name.c_str(), s->desc.size);
		return;
	}
	// 버퍼는 COMMON 에서 COPY_* 로 암묵적으로 승격되므로 배리어가 필요 없음.
	m_list->CopyResource(d->resource.Get(), s->resource.Get());
}

void D3D12CommandList::WriteTimestamp(uint32_t slot)
{
	if (m_device) m_device->WriteTimestamp(slot);
}

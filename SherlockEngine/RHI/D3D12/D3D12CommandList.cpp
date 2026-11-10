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
	for (BoundSet& bound : m_sets) bound = BoundSet{};
}

void D3D12CommandList::BeginFrame(ID3D12GraphicsCommandList* list)
{
	m_list = list;
	m_inPass = false;
	m_currentPipeline = PipelineHandle{};
	m_currentPso = nullptr;
	m_currentRoot = nullptr;
	// 기억해 둔 바인딩은 유지하되 (Renderer 가 프레임 초에 다시 설정함) 디스크립터 테이블은 다시 바인딩해야 함.
	for (BoundSet& bound : m_sets) bound.tablesDirty = true;
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
	m_passWidth = width;
	m_passHeight = height;

	// 바인딩. flip 모델에서는 Present 후 백버퍼 바인딩이 풀리므로 패스마다 다시 바인딩함.
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
	ApplyViewport(desc.viewport);
	/*D3D12_VIEWPORT viewport = {};
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
	m_list->RSSetScissorRects(1, &scissor);*/
	
}

void D3D12CommandList::ApplyViewport(const Viewport& viewport)
{
	D3D12_VIEWPORT vp = {};
	vp.TopLeftX = viewport.x;
	vp.TopLeftY = viewport.y;
	vp.Width = viewport.width > 0.0f ? viewport.width : static_cast<float>(m_passWidth);
	vp.Height = viewport.height > 0.0f ? viewport.height : static_cast<float>(m_passHeight);
	vp.MinDepth = viewport.minDepth;
	vp.MaxDepth = viewport.maxDepth;
	m_list->RSSetViewports(1, &vp);

	D3D12_RECT scissor = {};
	scissor.left = static_cast<LONG>(vp.TopLeftX);
	scissor.top = static_cast<LONG>(vp.TopLeftY);
	scissor.right = static_cast<LONG>(vp.TopLeftX + vp.Width);
	scissor.bottom = static_cast<LONG>(vp.TopLeftY + vp.Height);
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
		for (BoundSet& bound : m_sets) bound.tablesDirty = true;
	}
	m_list->IASetPrimitiveTopology(pso->GetTopology());
	m_currentPipeline = handle;
	m_currentPso = pso;
}

void D3D12CommandList::SetResourceSet(uint32_t setIndex, ResourceSetHandle handle)
{
	static bool warned = false;
	static bool warnedIndex = false;
	const D3D12ResourceSet* set = m_device ? m_device->GetResourceSet(handle) : nullptr;
	if (set == nullptr)
	{
		ErrorOnce(warned, "SetResourceSet : 유효하지 않은 ResourceSetHandle.");
		return;
	}
	if (setIndex >= kMaxBindingSets)
	{
		ErrorOnce(warnedIndex, "SetResourceSet : setIndex 가 kMaxBindingSets 이상. 이 셋은 바인딩되지 않음.");
		return;
	}
	// 인덱스 자리에 기억만 함. 실제 바인딩은 드로우 직전 (ApplyBindings) — 그때 현재 PSO 의 같은 인덱스
	// 레이아웃과 맞는지 확인함.
	BoundSet& bound = m_sets[setIndex];
	if (bound.set != handle)
	{
		bound.layout = set->desc.layout;
		bound.set = handle;
		bound.tablesDirty = true;
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

// 드로우 직전: 현재 루트 시그니처의 셋 순서대로 루트 CBV·루트 SRV(구조체 버퍼)와 테이블을 바인딩함.
// 루트 디스크립터는 드로우마다 다시 바인딩함 — 오브젝트 상수버퍼가 드로우마다 UpdateBuffer 로 새 링 주소를 받기 때문임.
// 테이블은 셋이나 루트 시그니처가 바뀌었을 때만 다시 바인딩함.
void D3D12CommandList::ApplyBindings()
{
	static bool warnedMissing = false;
	static bool warnedStale = false;
	static bool warnedLayout = false;
	if (m_currentRoot == nullptr) return;

	for (uint32_t s = 0; s < m_currentRoot->setCount; ++s)
	{
		const D3D12RootLayout::SetBinding& binding = m_currentRoot->sets[s];
		BoundSet* bound = &m_sets[s];
		if (!bound->set.IsValid())
		{
			ErrorOnce(warnedMissing, "DrawIndexed : PSO 가 요구하는 셋 인덱스에 ResourceSet 이 바인딩되지 않음.");
			continue;
		}
		if (bound->layout != binding.layout)
		{
			ErrorOnce(warnedLayout, "DrawIndexed : 셋 인덱스에 꽂힌 ResourceSet 의 레이아웃이 PSO 의 그 자리 BindingLayout 과 다름.");
			continue;
		}
		const D3D12ResourceSet* set = m_device->GetResourceSet(bound->set);
		const D3D12BindingLayout* layout = m_device->GetBindingLayout(binding.layout);
		if (set == nullptr || layout == nullptr) continue;

		for (uint32_t i = 0; i < layout->desc.slotCount; ++i)
		{
			const BindingType type = layout->desc.slots[i].type;
			if (type != BindingType::ConstantBuffer && type != BindingType::BufferResource) continue;
			const uint32_t root = binding.descriptorRoot[i];
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
			if (type == BindingType::ConstantBuffer) m_list->SetGraphicsRootConstantBufferView(root, buffer->gpuAddress);
			else m_list->SetGraphicsRootShaderResourceView(root, buffer->gpuAddress);   // 구조체 버퍼. 원소 크기는 셰이더의 StructuredBuffer<T> 가 정함
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

void D3D12CommandList::Draw(uint32_t vertexCount, uint32_t startVertex)
{
	static bool warnedOutside = false;
	if (!m_inPass)
	{
		ErrorOnce(warnedOutside, "Draw : 렌더 패스 밖에서 드로우. BeginRenderPass 가 필요하다.");
		return;
	}
	if (m_list == nullptr || m_currentPso == nullptr) return;
	ApplyBindings();
	m_list->DrawInstanced(vertexCount, 1, startVertex, 0);
}

// ------------------------------------------------------------------ 전이·복사

void D3D12CommandList::Barrier(TextureHandle handle, ResourceState after)
{
	D3D12Texture* texture = m_device ? m_device->GetTexture(handle) : nullptr;
	if (texture == nullptr || m_list == nullptr)
	{
		static bool warned = false;
		ErrorOnce(warned, "Barrier : 유효하지 않은 TextureHandle.");
		return;
	}
	// before 는 텍스처가 기억하는 상태. 호출 코드가 따로 추적해 넘기지 않으므로 어긋날 수 없음.
	const ResourceState before = texture->state;
	if (before == after) return;   // 전이 없음 (통계에도 세지 않음)
	++m_stats.barriers;

	const D3D12_RESOURCE_BARRIER barrier = D3D12Convert::TransitionBarrier(
		texture->resource.Get(), D3D12Convert::ToD3D12(before), D3D12Convert::ToD3D12(after));
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

void D3D12CommandList::SetViewport(const Viewport& viewport)
{
	static bool warnedOutside = false;
	if (!m_inPass) { ErrorOnce(warnedOutside, "SetViewport : 렌더 패스 안에서만 부를 수 있음."); return; }
	ApplyViewport(viewport);
}


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

bool D3D12CommandList::CheckReadOnlyDepthDraw(const char* caller) const
{
	if (!m_passDepthReadOnly || m_currentPso == nullptr) return true;
	const DepthStencilDesc& ds = m_currentPso->GetDesc().depthStencil;
	if (!(ds.depthEnable && ds.depthWrite)) return true;   // 테스트를 끄면 쓰기도 일어나지 않음
	// 읽기 전용 DSV 에 깊이 쓰기 PSO 는 설정 실수임. 결과가 백엔드·드라이버마다 다를 수 있어 그리지 않음.
	static bool warned = false;
	if (!warned) Log::Error("%s : 읽기 전용 깊이 패스에서 깊이 쓰기 PSO 로 그림 — PSO 의 depthWrite 를 끌 것. 드로우를 건너뜀.", caller);
	warned = true;
	return false;
}

void D3D12CommandList::Init(D3D12Device* device)
{
	m_device = device;
	m_list = nullptr;
	m_inPass = false;
	m_passDepthReadOnly = false;
	for (BoundSet& bound : m_sets) bound = BoundSet{};
	for (BoundSet& bound : m_computeSets) bound = BoundSet{};
	m_touchedBuffers.clear();
}

void D3D12CommandList::BeginFrame(ID3D12GraphicsCommandList* list)
{
	m_list = list;
	m_inPass = false;
	m_passDepthReadOnly = false;
	m_currentPipeline = PipelineHandle{};
	m_currentPso = nullptr;
	m_currentRoot = nullptr;
	m_currentComputePipeline = ComputePipelineHandle{};
	m_currentComputePso = nullptr;
	m_currentComputeRoot = nullptr;
	// 기억해 둔 바인딩은 유지하되 (Renderer 가 프레임 초에 다시 설정함) 디스크립터 테이블은 다시 바인딩해야 함.
	for (BoundSet& bound : m_sets) bound.tablesDirty = true;
	for (BoundSet& bound : m_computeSets) bound.tablesDirty = true;
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
	m_passDepthReadOnly = false;   // 아래에서 읽기 전용 DSV 를 실제로 바인딩할 때만 켬
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
		else if (desc.depth.readOnly)
		{
			// 경고만 하고 진행함 (상태가 틀리면 디버그 레이어가 따로 알림).
			static bool warnedState = false;
			static bool warnedLoad = false;
			if (texture->state != ResourceState::DepthRead && !warnedState)
			{
				warnedState = true;
				Log::Error("BeginRenderPass : 읽기 전용 깊이 '%s' 가 %s 상태 — Barrier(tex, DepthRead) 가 빠졌음 (%s).", texture->name.c_str(), ToString(texture->state), desc.debugName ? desc.debugName : "");
			}
			if (desc.depth.load != LoadOp::Load && !warnedLoad)
			{
				warnedLoad = true;
				Log::Error("BeginRenderPass : 읽기 전용 깊이는 LoadOp::Load 만 됨 — 지우지 않고 시작함 (%s).", desc.debugName ? desc.debugName : "");
			}
			// 할당에 실패하면 깊이 없이 감 (쓰기 DSV 로 대신하지 않음 — DEPTH_READ 상태에서 쓰기 DSV 는 상태 불일치).
			if (m_device->GetReadOnlyDSV(*texture, dsv))
			{
				hasDsv = true;
				m_passDepthReadOnly = true;
			}
			if (width == 0) { width = texture->desc.width; height = texture->desc.height; }
		}
		else
		{
			static bool warnedState = false;
			if (texture->state == ResourceState::DepthRead && !warnedState)
			{
				warnedState = true;
				Log::Error("BeginRenderPass : 깊이 '%s' 가 DepthRead 상태인데 쓰기 패스임 — readOnly 를 켜거나 Barrier(tex, DepthWrite) (%s).", texture->name.c_str(), desc.debugName ? desc.debugName : "");
			}
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
	if (hasDsv && desc.depth.load == LoadOp::Clear && !desc.depth.readOnly)   // 읽기 전용 DSV 는 지울 수 없음
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
	m_passDepthReadOnly = false;
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
	// PSO 자리는 그래픽스·컴퓨트가 함께 씀. 다음 SetComputePipeline 이 같은 핸들이라고 건너뛰지 않게 함.
	m_currentComputePipeline = ComputePipelineHandle{};
	m_currentComputePso = nullptr;
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

// 드로우·Dispatch 직전: 현재 루트 시그니처의 셋 순서대로 루트 CBV·루트 SRV(구조체 버퍼)와 테이블을 바인딩함.
// 루트 디스크립터는 매번 다시 바인딩함 — 오브젝트 상수버퍼가 드로우마다 UpdateBuffer 로 새 링 주소를 받기 때문임
// (컴퓨트도 같음: 블러 두 번의 Dispatch 사이에 PostParams 가 새 주소를 받음).
// 테이블은 셋이나 루트 시그니처가 바뀌었을 때만 다시 바인딩함.
void D3D12CommandList::ApplyBindings(bool compute, const char* caller)
{
	static bool warnedMissing = false;
	static bool warnedStale = false;
	static bool warnedLayout = false;
	const D3D12RootLayout* rootLayout = compute ? m_currentComputeRoot : m_currentRoot;
	BoundSet* sets = compute ? m_computeSets : m_sets;
	if (rootLayout == nullptr) return;

	for (uint32_t s = 0; s < rootLayout->setCount; ++s)
	{
		const D3D12RootLayout::SetBinding& binding = rootLayout->sets[s];
		BoundSet* bound = &sets[s];
		if (!bound->set.IsValid())
		{
			if (!warnedMissing) Log::Error("%s : PSO 가 요구하는 셋 인덱스 %u 에 ResourceSet 이 바인딩되지 않음.", caller, s);
			warnedMissing = true;
			continue;
		}
		if (bound->layout != binding.layout)
		{
			if (!warnedLayout) Log::Error("%s : 셋 인덱스 %u 에 꽂힌 ResourceSet 의 레이아웃이 PSO 의 그 자리 BindingLayout 과 다름.", caller, s);
			warnedLayout = true;
			continue;
		}
		const D3D12ResourceSet* set = m_device->GetResourceSet(bound->set);
		const D3D12BindingLayout* layout = m_device->GetBindingLayout(binding.layout);
		if (set == nullptr || layout == nullptr) continue;

		for (uint32_t i = 0; i < layout->desc.slotCount; ++i)
		{
			const BindingType type = layout->desc.slots[i].type;
			if (type == BindingType::BufferUnorderedAccess || type == BindingType::RawBufferUnorderedAccess)
			{
				// 테이블에 있으므로 바인딩은 아래에서 함. 여기서는 상태만 검사 (D3D11 백엔드와 같은 규칙).
				const D3D12Buffer* buffer = m_device->GetBuffer(set->desc.bindings[i].buffer);
				static bool warnedUav = false;
				if (buffer != nullptr && buffer->state != ResourceState::UnorderedAccess && !warnedUav)
				{
					warnedUav = true;
					Log::Error("%s : UAV 로 쓰는 버퍼 '%s' 가 %s 상태 — Barrier(buf, UnorderedAccess) 가 빠졌음.", caller, buffer->name.c_str(), ToString(buffer->state));
				}
				continue;
			}
			if (type == BindingType::RawBufferResource)
			{
				// raw SRV 도 테이블에 있음 (루트 SRV 가 아님). 상태 검사만 — BufferResource 와 같은 규칙.
				const D3D12Buffer* buffer = m_device->GetBuffer(set->desc.bindings[i].buffer);
				static bool warnedRawSrv = false;
				if (buffer != nullptr && IsStateTrackedBuffer(buffer->desc) && buffer->state != ResourceState::ShaderResource && !warnedRawSrv)
				{
					warnedRawSrv = true;
					Log::Error("%s : SRV 로 읽는 UAV·간접 인자 버퍼 '%s' 가 %s 상태 — Barrier(buf, ShaderResource) 가 빠졌음 (프레임마다 Common 으로 돌아감).", caller, buffer->name.c_str(), ToString(buffer->state));
				}
				continue;
			}
			if (type != BindingType::ConstantBuffer && type != BindingType::BufferResource) continue;
			const uint32_t root = binding.descriptorRoot[i];
			if (root == UINT32_MAX) continue;
			const D3D12Buffer* buffer = m_device->GetBuffer(set->desc.bindings[i].buffer);
			if (buffer == nullptr || buffer->gpuAddress == 0)
			{
				if (!warnedStale) Log::Error("%s : 상수버퍼가 아직 UpdateBuffer 되지 않음 (Dynamic 은 프레임마다 써야 한다).", caller);
				warnedStale = true;
				continue;
			}
			if (type == BindingType::BufferResource && IsStateTrackedBuffer(buffer->desc) && buffer->state != ResourceState::ShaderResource)
			{
				// Common 버퍼를 SRV 로 읽으면 암묵 승격되어 추적 상태와 실제 상태가 어긋남 (다음 배리어의 before 가 틀림).
				static bool warnedSrv = false;
				if (!warnedSrv) Log::Error("%s : SRV 로 읽는 UAV·간접 인자 버퍼 '%s' 가 %s 상태 — Barrier(buf, ShaderResource) 가 빠졌음 (프레임마다 Common 으로 돌아감).", caller, buffer->name.c_str(), ToString(buffer->state));
				warnedSrv = true;
			}
			if (buffer->desc.usage == BufferUsage::Dynamic && buffer->updatedFrame != m_device->GetFrameNumber())
			{
				if (!warnedStale) Log::Error("%s : Dynamic 상수버퍼가 이번 프레임에 갱신되지 않음. 이전 프레임의 링 조각은 재사용될 수 있다.", caller);
				warnedStale = true;
			}
			// 구조체 버퍼의 원소 크기는 셰이더의 StructuredBuffer<T> 가 정함
			if (compute)
			{
				if (type == BindingType::ConstantBuffer) m_list->SetComputeRootConstantBufferView(root, buffer->gpuAddress);
				else m_list->SetComputeRootShaderResourceView(root, buffer->gpuAddress);
			}
			else
			{
				if (type == BindingType::ConstantBuffer) m_list->SetGraphicsRootConstantBufferView(root, buffer->gpuAddress);
				else m_list->SetGraphicsRootShaderResourceView(root, buffer->gpuAddress);
			}
		}
		if (bound->tablesDirty)
		{
			if (binding.srvTableRoot != UINT32_MAX && set->srvCount > 0)
			{
				const D3D12_GPU_DESCRIPTOR_HANDLE table = m_device->GetSrvHeap().Gpu(set->srvTableStart);
				if (compute) m_list->SetComputeRootDescriptorTable(binding.srvTableRoot, table);
				else m_list->SetGraphicsRootDescriptorTable(binding.srvTableRoot, table);
			}
			if (binding.samplerTableRoot != UINT32_MAX && set->samplerCount > 0)
			{
				const D3D12_GPU_DESCRIPTOR_HANDLE table = m_device->GetSamplerHeap().Gpu(set->samplerTableStart);
				if (compute) m_list->SetComputeRootDescriptorTable(binding.samplerTableRoot, table);
				else m_list->SetGraphicsRootDescriptorTable(binding.samplerTableRoot, table);
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
	if (!CheckReadOnlyDepthDraw("DrawIndexed")) return;
	ApplyBindings(false, "DrawIndexed");
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
	if (!CheckReadOnlyDepthDraw("Draw")) return;
	ApplyBindings(false, "Draw");
	m_list->DrawInstanced(vertexCount, 1, startVertex, 0);
}

ID3D12Resource* D3D12CommandList::GetIndirectArgs(BufferHandle handle, uint32_t offsetBytes, uint32_t argBytes, const char* caller)
{
	static bool warnedInvalid = false;
	static bool warnedOffset = false;
	static bool warnedState = false;
	const D3D12Buffer* buffer = m_device ? m_device->GetBuffer(handle) : nullptr;
	if (buffer == nullptr || !(buffer->desc.bindFlags & BufferBind_IndirectArgs) || !buffer->resource)
	{
		if (!warnedInvalid) Log::Error("%s : 인자 버퍼가 유효하지 않거나 BufferBind_IndirectArgs 로 만들지 않았음.", caller);
		warnedInvalid = true;
		return nullptr;
	}
	if (offsetBytes % 4 != 0 || static_cast<uint64_t>(offsetBytes) + argBytes > buffer->desc.size)
	{
		if (!warnedOffset) Log::Error("%s : 인자 오프셋 %u 가 4 의 배수가 아니거나 버퍼 '%s' (%u바이트) 범위 밖.", caller, offsetBytes, buffer->name.c_str(), buffer->desc.size);
		warnedOffset = true;
		return nullptr;
	}
	if (buffer->state != ResourceState::IndirectArgument && !warnedState)
	{
		// ApplyBindings 의 상태 검사와 같이 경고만 함 (디버그 레이어가 실제 상태 불일치를 따로 알림).
		warnedState = true;
		Log::Error("%s : 인자 버퍼 '%s' 가 %s 상태 — Barrier(buf, IndirectArgument) 가 빠졌음.", caller, buffer->name.c_str(), ToString(buffer->state));
	}
	return buffer->resource.Get();
}

void D3D12CommandList::DrawInstancedIndirect(BufferHandle args, uint32_t offsetBytes)
{
	static bool warnedOutside = false;
	static bool warnedSignature = false;
	if (!m_inPass)
	{
		ErrorOnce(warnedOutside, "DrawInstancedIndirect : 렌더 패스 밖에서 드로우. BeginRenderPass 가 필요하다.");
		return;
	}
	if (m_list == nullptr || m_currentPso == nullptr) return;
	if (!CheckReadOnlyDepthDraw("DrawInstancedIndirect")) return;
	ID3D12CommandSignature* signature = m_device->GetDrawIndirectSignature();
	if (signature == nullptr)
	{
		ErrorOnce(warnedSignature, "DrawInstancedIndirect : draw 커맨드 시그니처가 없음 (장치 초기화 때 생성 실패).");
		return;
	}
	ID3D12Resource* resource = GetIndirectArgs(args, offsetBytes, static_cast<uint32_t>(sizeof(D3D12_DRAW_ARGUMENTS)), "DrawInstancedIndirect");
	if (resource == nullptr) return;
	ApplyBindings(false, "DrawInstancedIndirect");
	m_list->ExecuteIndirect(signature, 1, resource, offsetBytes, nullptr, 0);
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
	if (after == ResourceState::DepthRead && !(texture->desc.bindFlags & TextureBind_DepthStencil))
	{
		// DEPTH_READ 는 ALLOW_DEPTH_STENCIL 리소스에만 쓸 수 있음.
		static bool warnedDepth = false;
		if (!warnedDepth) Log::Error("Barrier : 텍스처 '%s' 는 TextureBind_DepthStencil 이 아니라 DepthRead 로 전이할 수 없음.", texture->name.c_str());
		warnedDepth = true;
		return;
	}
	// before 는 텍스처가 기억하는 상태. 호출 코드가 따로 추적해 넘기지 않으므로 어긋날 수 없음.
	const ResourceState before = texture->state;
	if (before == after) return;   // 전이 없음 (통계에도 세지 않음)
	++m_stats.barriers;

	// SRV 없는 깊이는 DENY_SHADER_RESOURCE 로 만들어져 셰이더 리소스 상태 비트를 가질 수 없음 — DepthRead 에서 그 비트를 뺌.
	// before 도 같은 규칙으로 바꿔야 직전 배리어의 after 와 정확히 맞음.
	const bool denySrv = !(texture->desc.bindFlags & TextureBind_ShaderResource);
	auto toD3D12 = [denySrv](ResourceState state)
	{
		if (state == ResourceState::DepthRead && denySrv) return D3D12_RESOURCE_STATE_DEPTH_READ;
		return D3D12Convert::ToD3D12(state);
	};
	const D3D12_RESOURCE_BARRIER barrier = D3D12Convert::TransitionBarrier(
		texture->resource.Get(), toD3D12(before), toD3D12(after));
	m_list->ResourceBarrier(1, &barrier);
	texture->state = after;
}

void D3D12CommandList::UavBarrier(TextureHandle handle)
{
	D3D12Texture* texture = m_device ? m_device->GetTexture(handle) : nullptr;
	if (texture == nullptr || m_list == nullptr)
	{
		static bool warned = false;
		ErrorOnce(warned, "UavBarrier : 유효하지 않은 TextureHandle.");
		return;
	}
	if (texture->state != ResourceState::UnorderedAccess)
	{
		static bool warnedState = false;
		if (!warnedState) Log::Error("UavBarrier : '%s' 가 UnorderedAccess 상태가 아님 (%s). Barrier(UnorderedAccess) 가 먼저 필요함.", texture->name.c_str(), ToString(texture->state));
		warnedState = true;
		return;
	}
	// 같은 텍스처에 대한 앞 Dispatch 의 쓰기가 끝난 뒤에 다음 Dispatch 가 읽고 쓰게 함 (상태는 그대로).
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.UAV.pResource = texture->resource.Get();
	m_list->ResourceBarrier(1, &barrier);
	++m_stats.barriers;
}

void D3D12CommandList::SetComputePipeline(ComputePipelineHandle handle)
{
	static bool warned = false;
	const D3D12ComputePipelineState* pso = m_device ? m_device->GetComputePipeline(handle) : nullptr;
	if (pso == nullptr || m_list == nullptr)
	{
		ErrorOnce(warned, "SetComputePipeline : 유효하지 않은 ComputePipelineHandle.");
		return;
	}
	if (handle == m_currentComputePipeline) return;

	m_list->SetPipelineState(pso->Get());
	const D3D12RootLayout* root = pso->GetRootLayout();
	if (root != m_currentComputeRoot)
	{
		m_list->SetComputeRootSignature(root->rootSignature.Get());
		m_currentComputeRoot = root;
		for (BoundSet& bound : m_computeSets) bound.tablesDirty = true;
	}
	m_currentComputePipeline = handle;
	m_currentComputePso = pso;
	// PSO 자리를 컴퓨트가 차지했으므로 다음 SetPipelineState 는 같은 핸들이어도 다시 설정해야 함.
	// 그래픽스 루트 시그니처·루트 인자는 별개라 그대로 유지됨.
	m_currentPipeline = PipelineHandle{};
	m_currentPso = nullptr;
}

void D3D12CommandList::SetComputeResourceSet(uint32_t setIndex, ResourceSetHandle handle)
{
	static bool warned = false;
	static bool warnedIndex = false;
	const D3D12ResourceSet* set = m_device ? m_device->GetResourceSet(handle) : nullptr;
	if (set == nullptr)
	{
		ErrorOnce(warned, "SetComputeResourceSet : 유효하지 않은 ResourceSetHandle.");
		return;
	}
	if (setIndex >= kMaxBindingSets)
	{
		ErrorOnce(warnedIndex, "SetComputeResourceSet : setIndex 가 kMaxBindingSets 이상. 이 셋은 바인딩되지 않음.");
		return;
	}
	BoundSet& bound = m_computeSets[setIndex];
	if (bound.set != handle)
	{
		bound.layout = set->desc.layout;
		bound.set = handle;
		bound.tablesDirty = true;
	}
}

void D3D12CommandList::Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
{
	static bool warnedInPass = false;
	static bool warnedNoPso = false;
	if (m_inPass)
	{
		ErrorOnce(warnedInPass, "Dispatch : 렌더 패스 안에서 Dispatch. EndRenderPass 를 먼저 부를 것.");
		return;
	}
	if (m_list == nullptr) return;
	if (m_currentComputePso == nullptr)
	{
		ErrorOnce(warnedNoPso, "Dispatch : 컴퓨트 PSO 가 설정되지 않음 (SetComputePipeline 이 먼저 필요함).");
		return;
	}
	ApplyBindings(true, "Dispatch");
	m_list->Dispatch(groupsX, groupsY, groupsZ);
	++m_stats.dispatches;
}

void D3D12CommandList::DispatchIndirect(BufferHandle args, uint32_t offsetBytes)
{
	static bool warnedInPass = false;
	static bool warnedNoPso = false;
	static bool warnedSignature = false;
	if (m_inPass)
	{
		ErrorOnce(warnedInPass, "DispatchIndirect : 렌더 패스 안에서 Dispatch. EndRenderPass 를 먼저 부를 것.");
		return;
	}
	if (m_list == nullptr) return;
	if (m_currentComputePso == nullptr)
	{
		ErrorOnce(warnedNoPso, "DispatchIndirect : 컴퓨트 PSO 가 설정되지 않음 (SetComputePipeline 이 먼저 필요함).");
		return;
	}
	ID3D12CommandSignature* signature = m_device->GetDispatchIndirectSignature();
	if (signature == nullptr)
	{
		ErrorOnce(warnedSignature, "DispatchIndirect : dispatch 커맨드 시그니처가 없음 (장치 초기화 때 생성 실패).");
		return;
	}
	ID3D12Resource* resource = GetIndirectArgs(args, offsetBytes, static_cast<uint32_t>(sizeof(D3D12_DISPATCH_ARGUMENTS)), "DispatchIndirect");
	if (resource == nullptr) return;
	ApplyBindings(true, "DispatchIndirect");
	m_list->ExecuteIndirect(signature, 1, resource, offsetBytes, nullptr, 0);
	++m_stats.dispatches;
}

void D3D12CommandList::TrackBufferState(BufferHandle handle, D3D12Buffer* buffer, ResourceState after)
{
	if (buffer->state == ResourceState::Common && after != ResourceState::Common) m_touchedBuffers.push_back(handle);
	buffer->state = after;
}

void D3D12CommandList::ResetBufferStates()
{
	for (BufferHandle handle : m_touchedBuffers)
	{
		if (D3D12Buffer* buffer = m_device->GetBufferMutable(handle)) buffer->state = ResourceState::Common;
	}
	m_touchedBuffers.clear();
}

void D3D12CommandList::Barrier(BufferHandle handle, ResourceState after)
{
	D3D12Buffer* buffer = m_device ? m_device->GetBufferMutable(handle) : nullptr;
	if (buffer == nullptr || m_list == nullptr)
	{
		static bool warned = false;
		ErrorOnce(warned, "Barrier : 유효하지 않은 BufferHandle.");
		return;
	}
	if (!buffer->resource)
	{
		static bool warnedDynamic = false;
		if (!warnedDynamic) Log::Error("Barrier : 버퍼 '%s' 는 리소스가 없는 Dynamic 버퍼라 상태가 없음.", buffer->name.c_str());
		warnedDynamic = true;
		return;
	}
	if (after != ResourceState::Common && after != ResourceState::ShaderResource && after != ResourceState::UnorderedAccess &&
		after != ResourceState::IndirectArgument)
	{
		static bool warnedState = false;
		if (!warnedState) Log::Error("Barrier : 버퍼 '%s' 에 쓸 수 없는 상태 %s (Common · ShaderResource · UnorderedAccess · IndirectArgument 만).", buffer->name.c_str(), ToString(after));
		warnedState = true;
		return;
	}
	if (after == ResourceState::UnorderedAccess && !(buffer->desc.bindFlags & BufferBind_UnorderedAccess))
	{
		static bool warnedFlag = false;
		if (!warnedFlag) Log::Error("Barrier : 버퍼 '%s' 는 BufferBind_UnorderedAccess 로 만들지 않았음.", buffer->name.c_str());
		warnedFlag = true;
		return;
	}
	if (after == ResourceState::IndirectArgument && !(buffer->desc.bindFlags & BufferBind_IndirectArgs))
	{
		static bool warnedIndirect = false;
		if (!warnedIndirect) Log::Error("Barrier : 버퍼 '%s' 는 BufferBind_IndirectArgs 로 만들지 않았음.", buffer->name.c_str());
		warnedIndirect = true;
		return;
	}
	// IndirectArgument 도 일반 전이 (UNORDERED_ACCESS → INDIRECT_ARGUMENT 가 컴퓨트 쓰기 → 간접 인자 읽기의 동기화).
	// TrackBufferState 가 이번 프레임 목록에 넣으므로 EndFrame 의 Common 복귀에도 포함됨.
	const ResourceState before = buffer->state;
	if (before == after) return;
	++m_stats.barriers;
	const D3D12_RESOURCE_BARRIER barrier = D3D12Convert::TransitionBarrier(
		buffer->resource.Get(), D3D12Convert::ToD3D12(before), D3D12Convert::ToD3D12(after));
	m_list->ResourceBarrier(1, &barrier);
	TrackBufferState(handle, buffer, after);
}

void D3D12CommandList::UavBarrier(BufferHandle handle)
{
	const D3D12Buffer* buffer = m_device ? m_device->GetBuffer(handle) : nullptr;
	if (buffer == nullptr || !buffer->resource || m_list == nullptr)
	{
		static bool warned = false;
		ErrorOnce(warned, "UavBarrier : 유효하지 않은 BufferHandle.");
		return;
	}
	if (buffer->state != ResourceState::UnorderedAccess)
	{
		static bool warnedState = false;
		if (!warnedState) Log::Error("UavBarrier : 버퍼 '%s' 가 UnorderedAccess 상태가 아님 (%s).", buffer->name.c_str(), ToString(buffer->state));
		warnedState = true;
		return;
	}
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.UAV.pResource = buffer->resource.Get();
	m_list->ResourceBarrier(1, &barrier);
	++m_stats.barriers;
}

void D3D12CommandList::CopyBuffer(BufferHandle dst, BufferHandle src)
{
	D3D12Buffer* d = m_device ? m_device->GetBufferMutable(dst) : nullptr;
	D3D12Buffer* s = m_device ? m_device->GetBufferMutable(src) : nullptr;
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
	// 버퍼는 COMMON 에서 COPY_* 로 암묵적으로 승격되므로 배리어가 필요 없음. 대신 Common (또는 이미 승격된 같은 복사
	// 상태)이어야 하고, 승격된 상태를 기록해 둠 — 그래야 뒤따르는 명시적 배리어의 before 가 맞음.
	if ((d->state != ResourceState::Common && d->state != ResourceState::CopyDest) ||
		(s->state != ResourceState::Common && s->state != ResourceState::CopySource))
	{
		Log::Error("CopyBuffer : 버퍼가 Common 상태가 아님 (%s %s, %s %s). Barrier(buf, Common) 이 먼저 필요함.",
			d->name.c_str(), ToString(d->state), s->name.c_str(), ToString(s->state));
		return;
	}
	m_list->CopyResource(d->resource.Get(), s->resource.Get());
	TrackBufferState(dst, d, ResourceState::CopyDest);
	TrackBufferState(src, s, ResourceState::CopySource);
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


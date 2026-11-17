#include "pch.h"
#include "RHI/D3D11/D3D11CommandList.h"
#include "RHI/D3D11/D3D11Device.h"
#include "RHI/D3D11/D3D11Convert.h"
#include "Core/Log.h"

void D3D11CommandList::ErrorOnce(bool& flag, const char* message)
{
	if (flag) return;
	flag = true;
	Log::Error("%s", message);
}

void D3D11CommandList::ApplyViewport(const Viewport& viewport)
{

	ID3D11DeviceContext* context = m_device ? m_device->GetContext() : nullptr;
	if (context == nullptr) return;

	// 뷰포트. PSO 밖의 동적 상태임. width/height 가 0 이면 이 패스의 첫 attachment 크기.
	D3D11_VIEWPORT vp = {};
	vp.TopLeftX = viewport.x;
	vp.TopLeftY = viewport.y;
	vp.Width = viewport.width > 0.0f ? viewport.width : static_cast<float>(m_passWidth);
	vp.Height = viewport.height > 0.0f ? viewport.height : static_cast<float>(m_passHeight);
	vp.MinDepth = viewport.minDepth;
	vp.MaxDepth = viewport.maxDepth;
	context->RSSetViewports(1, &vp);
}

void D3D11CommandList::Init(D3D11Device* device)
{
	m_device = device;
	m_inPass = false;
	for (TextureHandle& h : m_vsSrv) h = TextureHandle{};
	for (TextureHandle& h : m_psSrv) h = TextureHandle{};
	for (TextureHandle& h : m_csSrv) h = TextureHandle{};
	for (TextureHandle& h : m_csUav) h = TextureHandle{};
	for (BufferHandle& h : m_vsSrvBuf) h = BufferHandle{};
	for (BufferHandle& h : m_psSrvBuf) h = BufferHandle{};
	for (BufferHandle& h : m_csSrvBuf) h = BufferHandle{};
	for (BufferHandle& h : m_csUavBuf) h = BufferHandle{};
	m_touchedBuffers.clear();
	m_computePipelineSet = false;
	m_pipelineDepthWrite = false;
}

bool D3D11CommandList::CheckReadOnlyDepthDraw(const char* caller)
{
	if (!m_currentPass.depth.readOnly || !m_currentPass.depth.texture.IsValid() || !m_pipelineDepthWrite) return true;
	// 읽기 전용 DSV 에 깊이 쓰기 PSO 는 설정 실수임. 쓰기가 버려질지 검증 에러가 될지 백엔드·드라이버마다 다를 수 있어
	// 두 백엔드 모두 그리지 않음 — 실수가 화면에서 바로 보이게 함.
	static bool warned = false;
	if (!warned) Log::Error("%s : 읽기 전용 깊이 패스 (%s) 에서 깊이 쓰기 PSO 로 그림 — PSO 의 depthWrite 를 끌 것. 드로우를 건너뜀.",
		caller, m_currentPass.debugName ? m_currentPass.debugName : "");
	warned = true;
	return false;
}

// ------------------------------------------------------------------ 렌더 패스

void D3D11CommandList::BeginRenderPass(const RenderPassDesc& desc)
{
	static bool warnedNested = false;
	ID3D11DeviceContext* context = m_device ? m_device->GetContext() : nullptr;
	if (context == nullptr) return;
	if (m_inPass)
	{
		ErrorOnce(warnedNested, "BeginRenderPass : 이전 패스가 끝나지 않음. EndRenderPass 를 먼저 부를 것.");
		EndRenderPass();
	}
	m_currentPass = desc;
	m_inPass = true;
	++m_stats.renderPasses;

	// (1) 타깃이 될 텍스처가 SRV·UAV 슬롯에 남아 있으면 먼저 풂.
	for (uint32_t i = 0; i < desc.colorCount; ++i)
	{
		UnbindShaderResourcesOf(desc.colors[i].texture);
		UnbindUavsOf(desc.colors[i].texture);
	}
	// 읽기 전용 깊이는 풀지 않음: 읽기 전용 DSV 와 같은 리소스의 SRV 는 동시에 바인딩될 수 있고 (런타임도 겹침으로 보지 않음),
	// 이 패스의 PS 가 그 SRV 로 깊이를 읽음 (소프트 파티클). 남은 슬롯 기록은 다음 쓰기 패스가 이 줄에서 풂.
	if (desc.depth.texture.IsValid() && !desc.depth.readOnly) UnbindShaderResourcesOf(desc.depth.texture);

	// (2) 뷰를 모음. 뷰포트 기본 크기는 첫 attachment 에서 가져옴.
	ID3D11RenderTargetView* rtvs[kMaxRenderTargets] = {};
	uint32_t width = 0, height = 0;
	for (uint32_t i = 0; i < desc.colorCount; ++i)
	{
		D3D11Texture* texture = m_device->GetTexture(desc.colors[i].texture);
		if (texture == nullptr)
		{
			Log::Error("BeginRenderPass : 컬러 attachment %u 의 텍스처 핸들이 유효하지 않음 (%s).", i, desc.debugName ? desc.debugName : "");
			continue;
		}
		rtvs[i] = texture->GetRTV(m_device->GetDevice(), desc.colors[i].srgbView);
		if (width == 0) { width = texture->desc.width; height = texture->desc.height; }
	}
	ID3D11DepthStencilView* dsv = nullptr;
	if (desc.depth.texture.IsValid())
	{
		D3D11Texture* texture = m_device->GetTexture(desc.depth.texture);
		if (texture == nullptr)
		{
			Log::Error("BeginRenderPass : 깊이 attachment 의 텍스처 핸들이 유효하지 않음 (%s).", desc.debugName ? desc.debugName : "");
		}
		else if (desc.depth.readOnly)
		{
			// 상태·load 검사는 경고만 하고 진행함 (D3D11 은 그래도 맞게 그려짐 — CheckSrvState 와 같은 태도).
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
			// 만들지 못하면 깊이 없이 감. 쓰기 DSV 로 대신하면 위에서 풀지 않은 SRV 와 겹침.
			dsv = texture->GetReadOnlyDSV(m_device->GetDevice());
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
			dsv = texture->GetDSV(m_device->GetDevice());
			if (width == 0) { width = texture->desc.width; height = texture->desc.height; }
		}
	}
	m_passWidth = width;
	m_passHeight = height;

	// (3) 바인딩. flip 모델에서는 Present 후 백버퍼 바인딩이 풀리므로 패스마다 다시 바인딩함.
	context->OMSetRenderTargets(desc.colorCount, desc.colorCount ? rtvs : nullptr, dsv);

	// (4) Load op. Clear 만 실제로 동작하고, Load/DontCare 는 D3D11에서 동작이 같음.
	for (uint32_t i = 0; i < desc.colorCount; ++i)
	{
		if (rtvs[i] && desc.colors[i].load == LoadOp::Clear)
		{
			context->ClearRenderTargetView(rtvs[i], desc.colors[i].clearColor);
		}
	}
	if (dsv && desc.depth.load == LoadOp::Clear && !desc.depth.readOnly)   // 읽기 전용 DSV 는 지울 수 없음
	{
		context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, desc.depth.clearDepth, desc.depth.clearStencil);
	}

	// (5) 뷰포트. PSO 밖의 동적 상태임.
	ApplyViewport(desc.viewport);

	/*D3D11_VIEWPORT viewport = {};
	viewport.TopLeftX = desc.viewport.x;
	viewport.TopLeftY = desc.viewport.y;
	viewport.Width = desc.viewport.width > 0.0f ? desc.viewport.width : static_cast<float>(width);
	viewport.Height = desc.viewport.height > 0.0f ? desc.viewport.height : static_cast<float>(height);
	viewport.MinDepth = desc.viewport.minDepth;
	viewport.MaxDepth = desc.viewport.maxDepth;
	context->RSSetViewports(1, &viewport);*/
}

void D3D11CommandList::EndRenderPass()
{
	ID3D11DeviceContext* context = m_device ? m_device->GetContext() : nullptr;
	if (!m_inPass || context == nullptr) return;
	// Store op 는 D3D11에서 할 일이 없음. 타깃을 풀어 다음 패스가 SRV 로 읽어도 경고가 나지 않게 함.
	context->OMSetRenderTargets(0, nullptr, nullptr);
	m_inPass = false;
}

void D3D11CommandList::UnbindShaderResourcesOf(TextureHandle texture)
{
	ID3D11DeviceContext* context = m_device->GetContext();
	ID3D11ShaderResourceView* nullSrv = nullptr;
	for (uint32_t slot = 0; slot < kMaxSrvRegister; ++slot)
	{
		if (m_vsSrv[slot] == texture)
		{
			context->VSSetShaderResources(slot, 1, &nullSrv);
			m_vsSrv[slot] = TextureHandle{};
			++m_stats.srvUnbinds;
		}
		if (m_psSrv[slot] == texture)
		{
			context->PSSetShaderResources(slot, 1, &nullSrv);
			m_psSrv[slot] = TextureHandle{};
			++m_stats.srvUnbinds;
		}
		if (m_csSrv[slot] == texture)
		{
			context->CSSetShaderResources(slot, 1, &nullSrv);
			m_csSrv[slot] = TextureHandle{};
			++m_stats.srvUnbinds;
		}
	}
}

void D3D11CommandList::UnbindUavsOf(TextureHandle texture)
{
	ID3D11DeviceContext* context = m_device->GetContext();
	ID3D11UnorderedAccessView* nullUav = nullptr;
	for (uint32_t slot = 0; slot < kMaxUavRegister; ++slot)
	{
		if (m_csUav[slot] == texture)
		{
			context->CSSetUnorderedAccessViews(slot, 1, &nullUav, nullptr);
			m_csUav[slot] = TextureHandle{};
			++m_stats.uavUnbinds;
		}
	}
}

void D3D11CommandList::CheckSrvState(const D3D11Texture* texture)
{
	// UnorderedAccess 만 거부함. DepthRead 는 SRV 읽기를 포함하는 상태라 통과 (읽기 전용 깊이 + 소프트 파티클·컴퓨트 충돌).
	// Common 등은 D3D11 텍스처가 배리어 없이 머무는 기본 상태라 여기서 막지 않음.
	static bool warned = false;
	if (texture != nullptr && texture->state == ResourceState::UnorderedAccess)
	{
		if (!warned) Log::Error("SRV 로 읽는 텍스처 '%s' 가 아직 UnorderedAccess 상태 — Barrier(tex, ShaderResource) 가 빠졌음 (D3D12 에서는 잘못된 결과).", texture->name.c_str());
		warned = true;
	}
}

void D3D11CommandList::CheckUavState(const D3D11Texture* texture)
{
	static bool warned = false;
	if (texture != nullptr && texture->state != ResourceState::UnorderedAccess)
	{
		if (!warned) Log::Error("UAV 로 쓰는 텍스처 '%s' 가 %s 상태 — Barrier(tex, UnorderedAccess) 가 빠졌음.", texture->name.c_str(), ToString(texture->state));
		warned = true;
	}
}

void D3D11CommandList::UnbindShaderResourcesOf(BufferHandle buffer)
{
	ID3D11DeviceContext* context = m_device->GetContext();
	ID3D11ShaderResourceView* nullSrv = nullptr;
	for (uint32_t slot = 0; slot < kMaxSrvRegister; ++slot)
	{
		if (m_vsSrvBuf[slot] == buffer) { context->VSSetShaderResources(slot, 1, &nullSrv); m_vsSrvBuf[slot] = BufferHandle{}; ++m_stats.srvUnbinds; }
		if (m_psSrvBuf[slot] == buffer) { context->PSSetShaderResources(slot, 1, &nullSrv); m_psSrvBuf[slot] = BufferHandle{}; ++m_stats.srvUnbinds; }
		if (m_csSrvBuf[slot] == buffer) { context->CSSetShaderResources(slot, 1, &nullSrv); m_csSrvBuf[slot] = BufferHandle{}; ++m_stats.srvUnbinds; }
	}
}

void D3D11CommandList::UnbindUavsOf(BufferHandle buffer)
{
	ID3D11DeviceContext* context = m_device->GetContext();
	ID3D11UnorderedAccessView* nullUav = nullptr;
	for (uint32_t slot = 0; slot < kMaxUavRegister; ++slot)
	{
		if (m_csUavBuf[slot] == buffer)
		{
			context->CSSetUnorderedAccessViews(slot, 1, &nullUav, nullptr);
			m_csUavBuf[slot] = BufferHandle{};
			++m_stats.uavUnbinds;
		}
	}
}

void D3D11CommandList::CheckSrvState(const D3D11Buffer* buffer)
{
	static bool warned = false;
	if (buffer != nullptr && IsStateTrackedBuffer(buffer->desc) && buffer->state != ResourceState::ShaderResource)
	{
		if (!warned) Log::Error("SRV 로 읽는 UAV·간접 인자 버퍼 '%s' 가 %s 상태 — Barrier(buf, ShaderResource) 가 빠졌음 (프레임마다 Common 으로 돌아감).", buffer->name.c_str(), ToString(buffer->state));
		warned = true;
	}
}

void D3D11CommandList::CheckUavState(const D3D11Buffer* buffer)
{
	static bool warned = false;
	if (buffer != nullptr && buffer->state != ResourceState::UnorderedAccess)
	{
		if (!warned) Log::Error("UAV 로 쓰는 버퍼 '%s' 가 %s 상태 — Barrier(buf, UnorderedAccess) 가 빠졌음.", buffer->name.c_str(), ToString(buffer->state));
		warned = true;
	}
}

void D3D11CommandList::TrackBufferState(BufferHandle handle, D3D11Buffer* buffer, ResourceState after)
{
	if (buffer->state == ResourceState::Common && after != ResourceState::Common) m_touchedBuffers.push_back(handle);
	buffer->state = after;
}

void D3D11CommandList::ResetBufferStates()
{
	for (BufferHandle handle : m_touchedBuffers)
	{
		if (D3D11Buffer* buffer = m_device->GetBufferMutable(handle)) buffer->state = ResourceState::Common;
	}
	m_touchedBuffers.clear();
}

// ------------------------------------------------------------------ 상태·바인딩

void D3D11CommandList::SetPipelineState(PipelineHandle handle)
{
	static bool warned = false;
	const PipelineState* state = m_device ? m_device->GetPipeline(handle) : nullptr;
	if (state == nullptr)
	{
		ErrorOnce(warned, "SetPipelineState : 유효하지 않은 PipelineHandle.");
		return;
	}
	state->Bind(m_device->GetContext());
	const DepthStencilDesc& ds = state->GetDesc().depthStencil;
	m_pipelineDepthWrite = ds.depthEnable && ds.depthWrite;   // 테스트를 끄면 쓰기도 일어나지 않음
}

void D3D11CommandList::SetResourceSet(uint32_t setIndex, ResourceSetHandle handle)
{
	// D3D11 에는 루트 시그니처가 없어 셋 인덱스가 뜻이 없음 — 슬롯의 레지스터 번호로 바로 바인딩함.
	(void)setIndex;
	static bool warned = false;
	const D3D11ResourceSet* set = m_device ? m_device->GetResourceSet(handle) : nullptr;
	const D3D11BindingLayout* layout = set ? m_device->GetBindingLayout(set->desc.layout) : nullptr;
	if (set == nullptr || layout == nullptr)
	{
		ErrorOnce(warned, "SetResourceSet : 유효하지 않은 ResourceSetHandle 또는 레이아웃.");
		return;
	}
	ID3D11DeviceContext* context = m_device->GetContext();
	const uint32_t frameIndex = m_device->GetFrameIndex();

	// D3D12라면 SetGraphicsRootDescriptorTable 한 번이면 됨. D3D11은 슬롯마다 스테이지별 호출로 풀어냄.
	for (uint32_t i = 0; i < layout->desc.slotCount; ++i)
	{
		const BindingSlot& slot = layout->desc.slots[i];
		const ResourceBinding& binding = set->desc.bindings[i];
		switch (slot.type)
		{
		case BindingType::ConstantBuffer:
		{
			const D3D11Buffer* buffer = m_device->GetBuffer(binding.buffer);
			ID3D11Buffer* d3dBuffer = buffer ? buffer->Get(frameIndex) : nullptr;
			if (slot.stageMask & ShaderStageMask_Vertex) context->VSSetConstantBuffers(slot.reg, 1, &d3dBuffer);
			if (slot.stageMask & ShaderStageMask_Pixel)  context->PSSetConstantBuffers(slot.reg, 1, &d3dBuffer);
			break;
		}
		case BindingType::ShaderResource:
		{
			D3D11Texture* texture = m_device->GetTexture(binding.texture);
			CheckSrvState(texture);
			ID3D11ShaderResourceView* srv = texture ? texture->GetSRV(m_device->GetDevice()) : nullptr;
			if (slot.stageMask & ShaderStageMask_Vertex)
			{
				context->VSSetShaderResources(slot.reg, 1, &srv);
				if (slot.reg < kMaxSrvRegister) { m_vsSrv[slot.reg] = binding.texture; m_vsSrvBuf[slot.reg] = BufferHandle{}; }
			}
			if (slot.stageMask & ShaderStageMask_Pixel)
			{
				context->PSSetShaderResources(slot.reg, 1, &srv);
				if (slot.reg < kMaxSrvRegister) { m_psSrv[slot.reg] = binding.texture; m_psSrvBuf[slot.reg] = BufferHandle{}; }
			}
			break;
		}
		case BindingType::BufferResource:
		case BindingType::RawBufferResource:   // raw 도 같은 경로 (GetSRV 가 뷰 종류를 고름)
		{
			// 구조체 버퍼. Dynamic 이면 이번 프레임 복제본의 SRV — UpdateBuffer 가 쓴 것과 같은 복제본임.
			D3D11Buffer* buffer = m_device->GetBufferMutable(binding.buffer);
			CheckSrvState(buffer);
			ID3D11ShaderResourceView* srv = buffer ? buffer->GetSRV(m_device->GetDevice(), frameIndex) : nullptr;
			if (slot.stageMask & ShaderStageMask_Vertex)
			{
				context->VSSetShaderResources(slot.reg, 1, &srv);
				if (slot.reg < kMaxSrvRegister) { m_vsSrv[slot.reg] = TextureHandle{}; m_vsSrvBuf[slot.reg] = binding.buffer; }   // 이 레지스터에는 이제 텍스처가 없음
			}
			if (slot.stageMask & ShaderStageMask_Pixel)
			{
				context->PSSetShaderResources(slot.reg, 1, &srv);
				if (slot.reg < kMaxSrvRegister) { m_psSrv[slot.reg] = TextureHandle{}; m_psSrvBuf[slot.reg] = binding.buffer; }
			}
			break;
		}
		case BindingType::Sampler:
		{
			const D3D11Sampler* sampler = m_device->GetSampler(binding.sampler);
			ID3D11SamplerState* state = sampler ? sampler->sampler.Get() : nullptr;
			if (slot.stageMask & ShaderStageMask_Vertex) context->VSSetSamplers(slot.reg, 1, &state);
			if (slot.stageMask & ShaderStageMask_Pixel)  context->PSSetSamplers(slot.reg, 1, &state);
			break;
		}
		case BindingType::UnorderedAccess:
		case BindingType::BufferUnorderedAccess:
		case BindingType::RawBufferUnorderedAccess:
			break;   // 컴퓨트 전용 (CreateBindingLayout 이 그래픽스 스테이지의 UAV 를 거부하므로 여기 오지 않음)
		}
	}
}

void D3D11CommandList::SetComputePipeline(ComputePipelineHandle handle)
{
	static bool warned = false;
	const D3D11ComputePipeline* pipeline = m_device ? m_device->GetComputePipeline(handle) : nullptr;
	if (pipeline == nullptr)
	{
		ErrorOnce(warned, "SetComputePipeline : 유효하지 않은 ComputePipelineHandle.");
		m_computePipelineSet = false;
		return;
	}
	m_device->GetContext()->CSSetShader(pipeline->cs.Get(), nullptr, 0);
	m_computePipelineSet = true;
}

void D3D11CommandList::SetComputeResourceSet(uint32_t setIndex, ResourceSetHandle handle)
{
	(void)setIndex;   // SetResourceSet 과 같음: D3D11 은 레지스터 번호로 바로 바인딩
	static bool warned = false;
	const D3D11ResourceSet* set = m_device ? m_device->GetResourceSet(handle) : nullptr;
	const D3D11BindingLayout* layout = set ? m_device->GetBindingLayout(set->desc.layout) : nullptr;
	if (set == nullptr || layout == nullptr)
	{
		ErrorOnce(warned, "SetComputeResourceSet : 유효하지 않은 ResourceSetHandle 또는 레이아웃.");
		return;
	}
	ID3D11DeviceContext* context = m_device->GetContext();
	const uint32_t frameIndex = m_device->GetFrameIndex();

	for (uint32_t i = 0; i < layout->desc.slotCount; ++i)
	{
		const BindingSlot& slot = layout->desc.slots[i];
		const ResourceBinding& binding = set->desc.bindings[i];
		switch (slot.type)
		{
		case BindingType::ConstantBuffer:
		{
			const D3D11Buffer* buffer = m_device->GetBuffer(binding.buffer);
			ID3D11Buffer* d3dBuffer = buffer ? buffer->Get(frameIndex) : nullptr;
			context->CSSetConstantBuffers(slot.reg, 1, &d3dBuffer);
			break;
		}
		case BindingType::ShaderResource:
		{
			D3D11Texture* texture = m_device->GetTexture(binding.texture);
			CheckSrvState(texture);
			ID3D11ShaderResourceView* srv = texture ? texture->GetSRV(m_device->GetDevice()) : nullptr;
			context->CSSetShaderResources(slot.reg, 1, &srv);
			if (slot.reg < kMaxSrvRegister) { m_csSrv[slot.reg] = binding.texture; m_csSrvBuf[slot.reg] = BufferHandle{}; }
			break;
		}
		case BindingType::BufferResource:
		case BindingType::RawBufferResource:
		{
			D3D11Buffer* buffer = m_device->GetBufferMutable(binding.buffer);
			CheckSrvState(buffer);
			ID3D11ShaderResourceView* srv = buffer ? buffer->GetSRV(m_device->GetDevice(), frameIndex) : nullptr;
			context->CSSetShaderResources(slot.reg, 1, &srv);
			if (slot.reg < kMaxSrvRegister) { m_csSrv[slot.reg] = TextureHandle{}; m_csSrvBuf[slot.reg] = binding.buffer; }
			break;
		}
		case BindingType::Sampler:
		{
			const D3D11Sampler* sampler = m_device->GetSampler(binding.sampler);
			ID3D11SamplerState* state = sampler ? sampler->sampler.Get() : nullptr;
			context->CSSetSamplers(slot.reg, 1, &state);
			break;
		}
		case BindingType::UnorderedAccess:
		{
			D3D11Texture* texture = m_device->GetTexture(binding.texture);
			CheckUavState(texture);
			ID3D11UnorderedAccessView* uav = texture ? texture->GetUAV(m_device->GetDevice(), binding.mip) : nullptr;
			context->CSSetUnorderedAccessViews(slot.reg, 1, &uav, nullptr);
			if (slot.reg < kMaxUavRegister) { m_csUav[slot.reg] = binding.texture; m_csUavBuf[slot.reg] = BufferHandle{}; }
			break;
		}
		case BindingType::BufferUnorderedAccess:
		case BindingType::RawBufferUnorderedAccess:   // GetUAV 가 구조체·raw 뷰를 고름
		{
			D3D11Buffer* buffer = m_device->GetBufferMutable(binding.buffer);
			CheckUavState(buffer);
			ID3D11UnorderedAccessView* uav = buffer ? buffer->GetUAV(m_device->GetDevice()) : nullptr;
			context->CSSetUnorderedAccessViews(slot.reg, 1, &uav, nullptr);
			if (slot.reg < kMaxUavRegister) { m_csUav[slot.reg] = TextureHandle{}; m_csUavBuf[slot.reg] = binding.buffer; }
			break;
		}
		}
	}
}

void D3D11CommandList::Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
{
	static bool warnedInPass = false;
	static bool warnedNoPipeline = false;
	if (m_inPass)
	{
		ErrorOnce(warnedInPass, "Dispatch : 렌더 패스 안에서 호출됨. EndRenderPass 뒤에 부를 것.");
		return;
	}
	if (!m_computePipelineSet)
	{
		ErrorOnce(warnedNoPipeline, "Dispatch : SetComputePipeline 이 먼저 필요함.");
		return;
	}
	if (groupsX == 0 || groupsY == 0 || groupsZ == 0) return;
	m_device->GetContext()->Dispatch(groupsX, groupsY, groupsZ);
	++m_stats.dispatches;
}

const D3D11Buffer* D3D11CommandList::GetIndirectArgs(BufferHandle handle, uint32_t offsetBytes, uint32_t argBytes, const char* caller)
{
	// D3D11 은 상태가 없지만 D3D12 와 같은 조건을 검사함 — 배리어가 빠진 호출은 D3D12 에서만 틀린 결과로 드러나기 때문.
	static bool warnedInvalid = false;
	static bool warnedOffset = false;
	static bool warnedState = false;
	const D3D11Buffer* buffer = m_device ? m_device->GetBuffer(handle) : nullptr;
	if (buffer == nullptr || !(buffer->desc.bindFlags & BufferBind_IndirectArgs) || !buffer->buffers[0])
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
		// D3D11 에서는 그래도 맞게 그려지므로 경고만 하고 진행함 (Draw 의 SRV 상태 검사와 같은 태도).
		warnedState = true;
		Log::Error("%s : 인자 버퍼 '%s' 가 %s 상태 — Barrier(buf, IndirectArgument) 가 빠졌음.", caller, buffer->name.c_str(), ToString(buffer->state));
	}
	return buffer;
}

void D3D11CommandList::DispatchIndirect(BufferHandle args, uint32_t offsetBytes)
{
	static bool warnedInPass = false;
	static bool warnedNoPipeline = false;
	if (m_inPass)
	{
		ErrorOnce(warnedInPass, "DispatchIndirect : 렌더 패스 안에서 호출됨. EndRenderPass 뒤에 부를 것.");
		return;
	}
	if (!m_computePipelineSet)
	{
		ErrorOnce(warnedNoPipeline, "DispatchIndirect : SetComputePipeline 이 먼저 필요함.");
		return;
	}
	const D3D11Buffer* buffer = GetIndirectArgs(args, offsetBytes, 12, "DispatchIndirect");
	if (buffer == nullptr) return;
	m_device->GetContext()->DispatchIndirect(buffer->buffers[0].Get(), offsetBytes);
	++m_stats.dispatches;
}

void D3D11CommandList::SetVertexBuffer(BufferHandle handle, uint32_t slot, uint32_t offset)
{
	static bool warned = false;
	const D3D11Buffer* buffer = m_device ? m_device->GetBuffer(handle) : nullptr;
	if (buffer == nullptr)
	{
		ErrorOnce(warned, "SetVertexBuffer : 유효하지 않은 BufferHandle.");
		return;
	}
	ID3D11Buffer* d3dBuffer = buffer->Get(m_device->GetFrameIndex());
	const UINT stride = buffer->desc.stride;
	m_device->GetContext()->IASetVertexBuffers(slot, 1, &d3dBuffer, &stride, &offset);
}

void D3D11CommandList::SetIndexBuffer(BufferHandle handle, Format indexFormat)
{
	static bool warned = false;
	const D3D11Buffer* buffer = m_device ? m_device->GetBuffer(handle) : nullptr;
	if (buffer == nullptr)
	{
		ErrorOnce(warned, "SetIndexBuffer : 유효하지 않은 BufferHandle.");
		return;
	}
	m_device->GetContext()->IASetIndexBuffer(buffer->Get(m_device->GetFrameIndex()), D3D11Convert::ToDXGI(indexFormat), 0);
}

// ------------------------------------------------------------------ 드로우

void D3D11CommandList::DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
{
	static bool warnedOutside = false;
	if (!m_inPass)
	{
		ErrorOnce(warnedOutside, "DrawIndexed : 렌더 패스 밖에서 드로우. BeginRenderPass 가 필요하다.");
		return;
	}
	if (!CheckReadOnlyDepthDraw("DrawIndexed")) return;
	m_device->GetContext()->DrawIndexed(indexCount, startIndex, baseVertex);
}

void D3D11CommandList::Draw(uint32_t vertexCount, uint32_t startVertex)
{
	static bool warnedOutside = false;
	if (!m_inPass)
	{
		ErrorOnce(warnedOutside, "Draw : 렌더 패스 밖에서 드로우. BeginRenderPass 가 필요하다.");
		return;
	}
	if (!CheckReadOnlyDepthDraw("Draw")) return;
	m_device->GetContext()->Draw(vertexCount, startVertex);
}

void D3D11CommandList::DrawInstancedIndirect(BufferHandle args, uint32_t offsetBytes)
{
	static bool warnedOutside = false;
	if (!m_inPass)
	{
		ErrorOnce(warnedOutside, "DrawInstancedIndirect : 렌더 패스 밖에서 드로우. BeginRenderPass 가 필요하다.");
		return;
	}
	if (!CheckReadOnlyDepthDraw("DrawInstancedIndirect")) return;
	const D3D11Buffer* buffer = GetIndirectArgs(args, offsetBytes, 16, "DrawInstancedIndirect");
	if (buffer == nullptr) return;
	m_device->GetContext()->DrawInstancedIndirect(buffer->buffers[0].Get(), offsetBytes);
}

// ------------------------------------------------------------------ 전이·복사

void D3D11CommandList::Barrier(TextureHandle handle, ResourceState after)
{
	D3D11Texture* texture = m_device ? m_device->GetTexture(handle) : nullptr;
	if (texture == nullptr)
	{
		static bool warned = false;
		ErrorOnce(warned, "Barrier : 유효하지 않은 TextureHandle.");
		return;
	}
	if (after == ResourceState::DepthRead && !(texture->desc.bindFlags & TextureBind_DepthStencil))
	{
		// D3D12 의 DEPTH_READ 는 깊이 리소스에만 쓸 수 있음. 같은 검사를 D3D11 에서도 해 두 백엔드가 같게 거부함.
		static bool warnedDepth = false;
		if (!warnedDepth) Log::Error("Barrier : 텍스처 '%s' 는 TextureBind_DepthStencil 이 아니라 DepthRead 로 전이할 수 없음.", texture->name.c_str());
		warnedDepth = true;
		return;
	}
	// D3D11 은 드라이버가 상태를 추적하므로 할 일이 없음. D3D12 와 같은 값을 기록만 해 둠 (디버그 표시용).
	// 통계는 D3D12 와 같게 실제로 상태가 바뀐 것만 셈.
	if (texture->state == after) return;
	// 예외: UAV 상태에 드나들 때는 겹치는 바인딩을 풂. 그대로 두면 런타임이 "still bound" 경고와 함께 다음 바인딩
	// 쪽을 NULL 로 만들어 셰이더가 0 을 읽음 (블러 핑퐁: 세로 패스의 UAV hdr 가 PS t13 에 남아 있는 경우 등).
	if (after == ResourceState::UnorderedAccess) UnbindShaderResourcesOf(handle);
	if (texture->state == ResourceState::UnorderedAccess) UnbindUavsOf(handle);
	++m_stats.barriers;
	texture->state = after;
}

void D3D11CommandList::UavBarrier(TextureHandle handle)
{
	// D3D11 은 같은 UAV 를 쓰는 Dispatch 사이에 드라이버가 대기를 넣으므로 할 일이 없음. 상태만 검사함 —
	// D3D12 에서 UAV 상태가 아닌 리소스에 UAV 배리어를 거는 것은 호출 실수이기 때문.
	D3D11Texture* texture = m_device ? m_device->GetTexture(handle) : nullptr;
	CheckUavState(texture);
}

void D3D11CommandList::Barrier(BufferHandle handle, ResourceState after)
{
	D3D11Buffer* buffer = m_device ? m_device->GetBufferMutable(handle) : nullptr;
	if (buffer == nullptr)
	{
		static bool warned = false;
		ErrorOnce(warned, "Barrier : 유효하지 않은 BufferHandle.");
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
	if (buffer->state == after) return;
	// 텍스처와 같은 규칙: UAV 상태에 드나들 때 겹치는 바인딩을 풂. 간접 인자로 읽을 때도 UAV 에 남아 있으면 안 됨 —
	// 런타임이 인자 버퍼와 겹치는 CS UAV 를 NULL 로 만들고 경고함 (UAV 상태를 거치지 않은 경로도 있으니 따로 풂).
	if (after == ResourceState::UnorderedAccess) UnbindShaderResourcesOf(handle);
	if (buffer->state == ResourceState::UnorderedAccess || after == ResourceState::IndirectArgument) UnbindUavsOf(handle);
	++m_stats.barriers;
	TrackBufferState(handle, buffer, after);
}

void D3D11CommandList::UavBarrier(BufferHandle handle)
{
	const D3D11Buffer* buffer = m_device ? m_device->GetBuffer(handle) : nullptr;
	CheckUavState(buffer);
}

void D3D11CommandList::CopyBuffer(BufferHandle dst, BufferHandle src)
{
	D3D11Buffer* d = m_device ? m_device->GetBufferMutable(dst) : nullptr;
	D3D11Buffer* s = m_device ? m_device->GetBufferMutable(src) : nullptr;
	if (d == nullptr || s == nullptr)
	{
		Log::Error("CopyBuffer : 유효하지 않은 BufferHandle.");
		return;
	}
	if (d->desc.size != s->desc.size)
	{
		Log::Error("CopyBuffer : 크기가 다름 (%s %u, %s %u).", d->name.c_str(), d->desc.size, s->name.c_str(), s->desc.size);
		return;
	}
	// D3D12 의 암묵 승격 규칙과 같게: Common (또는 이미 같은 복사 상태)에서만 복사하고, 복사 뒤 상태를 기록함.
	if ((d->state != ResourceState::Common && d->state != ResourceState::CopyDest) ||
		(s->state != ResourceState::Common && s->state != ResourceState::CopySource))
	{
		Log::Error("CopyBuffer : 버퍼가 Common 상태가 아님 (%s %s, %s %s). Barrier(buf, Common) 이 먼저 필요함.",
			d->name.c_str(), ToString(d->state), s->name.c_str(), ToString(s->state));
		return;
	}
	const uint32_t frameIndex = m_device->GetFrameIndex();
	m_device->GetContext()->CopyResource(d->Get(frameIndex), s->Get(frameIndex));
	TrackBufferState(dst, d, ResourceState::CopyDest);
	TrackBufferState(src, s, ResourceState::CopySource);
}

void D3D11CommandList::WriteTimestamp(uint32_t slot)
{
	if (m_device) m_device->WriteTimestamp(slot);
}

void D3D11CommandList::SetViewport(const Viewport& viewport)
{
	static bool warnedOutside = false;
	if (!m_inPass) { ErrorOnce(warnedOutside, "SetViewport : 렌더 패스 안에서만 부를 수 있음."); return; }
	ApplyViewport(viewport);
}

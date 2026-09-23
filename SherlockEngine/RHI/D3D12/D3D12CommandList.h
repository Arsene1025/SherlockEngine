#pragma once
#include "RHI/D3D12/D3D12Common.h"
#include "RHI/CommandList.h"
#include "RHI/BindingTypes.h"

class D3D12Device;
class D3D12PipelineState;
struct D3D12RootLayout;

// RHI::CommandList 의 D3D12 구현. Device 가 프레임마다 Reset 하는 ID3D12GraphicsCommandList 에 기록한다.
//
// D3D11 과 다른 점:
//   - Barrier 가 진짜 ResourceBarrier 다. before 는 텍스처의 추적 상태를 믿는다.
//   - SetResourceSet 은 바인딩을 "기억"만 하고, DrawIndexed 직전에 현재 PSO 의 루트 시그니처에 맞춰
//     루트 CBV(GPU 주소)와 디스크립터 테이블을 건다. Dynamic 상수버퍼의 주소가 UpdateBuffer 마다 바뀌기 때문이다.
//   - BeginRenderPass 는 시저 사각형도 건다 (D3D12 는 시저가 없으면 아무것도 그리지 않는다).
class D3D12CommandList final : public RHI::CommandList
{
public:
	void Init(D3D12Device* device);
	void BeginFrame(ID3D12GraphicsCommandList* list);   // Device::BeginFrame 이 Reset 한 리스트를 넘긴다

	void BeginRenderPass(const RenderPassDesc& desc) override;
	void EndRenderPass() override;
	bool IsInRenderPass() const override { return m_inPass; }

	void SetPipelineState(PipelineHandle handle) override;
	void SetResourceSet(ResourceSetHandle handle) override;
	void SetVertexBuffer(BufferHandle handle, uint32_t slot = 0, uint32_t offset = 0) override;
	void SetIndexBuffer(BufferHandle handle, Format indexFormat = Format::R32_UINT) override;

	void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0, int32_t baseVertex = 0) override;

	void Barrier(TextureHandle texture, ResourceState before, ResourceState after) override;
	void CopyBuffer(BufferHandle dst, BufferHandle src) override;

	void WriteTimestamp(uint32_t slot) override;   // 10단계: Device 의 쿼리 힙에 위임
	const Stats& GetStats() const override { return m_stats; }
	void ResetStats() override { m_stats = Stats{}; }

	ID3D12GraphicsCommandList* GetList() const { return m_list; }

private:
	void ApplyBindings();
	static void ErrorOnce(bool& flag, const char* message);

private:
	struct BoundSet
	{
		BindingLayoutHandle layout;
		ResourceSetHandle set;
		bool tablesDirty = true;
	};

	D3D12Device* m_device = nullptr;
	ID3D12GraphicsCommandList* m_list = nullptr;   // 소유권 없음 (Device 의 것)
	bool m_inPass = false;
	Stats m_stats;

	PipelineHandle m_currentPipeline;
	const D3D12PipelineState* m_currentPso = nullptr;
	const D3D12RootLayout* m_currentRoot = nullptr;
	BoundSet m_sets[kMaxBindingSets];
	uint32_t m_setCount = 0;
};

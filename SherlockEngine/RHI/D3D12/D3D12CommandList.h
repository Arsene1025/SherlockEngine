#pragma once
#include "RHI/D3D12/D3D12Common.h"
#include "RHI/CommandList.h"
#include "RHI/BindingTypes.h"
#include <vector>

class D3D12Device;
struct D3D12Buffer;
class D3D12PipelineState;
class D3D12ComputePipelineState;
struct D3D12RootLayout;

// RHI::CommandList 의 D3D12 구현. Device 가 프레임마다 Reset 하는 ID3D12GraphicsCommandList 에 기록함.
//
// D3D11 과 다른 점:
//   - Barrier 가 실제 ResourceBarrier 를 기록함. before 는 텍스처의 추적 상태(D3D12Texture::state)에서 얻음.
//   - SetResourceSet 은 바인딩을 "기억"만 하고, DrawIndexed 직전에 현재 PSO 의 루트 시그니처에 맞춰
//     루트 CBV(GPU 주소)와 디스크립터 테이블을 바인딩함. Dynamic 상수버퍼의 주소가 UpdateBuffer 마다 바뀌기 때문임.
//   - BeginRenderPass 는 시저 사각형도 설정함 (D3D12 는 시저 사각형이 없으면 아무것도 그리지 않음).
//   - 컴퓨트는 루트 시그니처·루트 인자를 그래픽스와 따로 가짐 (SetComputeRoot*). 그래서 셋 기억도 따로 둠.
//     PSO 자리는 하나뿐이라 SetComputePipeline 뒤에는 그래픽스 PSO 를 다시 설정해야 함 (반대도 같음).
class D3D12CommandList final : public RHI::CommandList
{
public:
	void Init(D3D12Device* device);
	void BeginFrame(ID3D12GraphicsCommandList* list);   // Device::BeginFrame 이 Reset 한 리스트를 넘겨받음

	void BeginRenderPass(const RenderPassDesc& desc) override;
	void EndRenderPass() override;
	bool IsInRenderPass() const override { return m_inPass; }

	void SetPipelineState(PipelineHandle handle) override;
	void SetResourceSet(uint32_t setIndex, ResourceSetHandle handle) override;
	void SetVertexBuffer(BufferHandle handle, uint32_t slot = 0, uint32_t offset = 0) override;
	void SetIndexBuffer(BufferHandle handle, Format indexFormat = Format::R32_UINT) override;

	void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0, int32_t baseVertex = 0) override;
	void Draw(uint32_t vertexCount, uint32_t startVertex = 0) override;

	void SetComputePipeline(ComputePipelineHandle handle) override;
	void SetComputeResourceSet(uint32_t setIndex, ResourceSetHandle handle) override;
	void Dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;

	void Barrier(TextureHandle texture, ResourceState after) override;
	void UavBarrier(TextureHandle texture) override;
	void Barrier(BufferHandle buffer, ResourceState after) override;
	void UavBarrier(BufferHandle buffer) override;
	void CopyBuffer(BufferHandle dst, BufferHandle src) override;

	void WriteTimestamp(uint32_t slot) override;   // Device 의 쿼리 힙에 위임
	const Stats& GetStats() const override { return m_stats; }
	void ResetStats() override { m_stats = Stats{}; }
	void SetViewport(const Viewport& viewport) override;

	ID3D12GraphicsCommandList* GetList() const { return m_list; }

	// ExecuteCommandLists 뒤 버퍼는 COMMON 으로 감쇠함. 이번 프레임에 상태가 바뀐 버퍼의 추적 상태를 Common 으로
	// 되돌림 (D3D12Device::EndFrame 이 실행 직후 부름).
	void ResetBufferStates();

private:
	// 드로우·Dispatch 직전에 현재 루트 시그니처에 맞춰 바인딩함. compute 면 SetComputeRoot*, 아니면 SetGraphicsRoot*.
	void ApplyBindings(bool compute, const char* caller);
	static void ErrorOnce(bool& flag, const char* message);

	void ApplyViewport(const Viewport& viewport);
	void TrackBufferState(BufferHandle handle, D3D12Buffer* buffer, ResourceState after);

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
	BoundSet m_sets[kMaxBindingSets];   // 인덱스 = PipelineStateDesc::bindingLayouts 의 인덱스. set 이 빈 핸들이면 아직 안 꽂힘

	ComputePipelineHandle m_currentComputePipeline;
	const D3D12ComputePipelineState* m_currentComputePso = nullptr;
	const D3D12RootLayout* m_currentComputeRoot = nullptr;
	BoundSet m_computeSets[kMaxBindingSets];   // 인덱스 = ComputePipelineDesc::bindingLayouts 의 인덱스
	std::vector<BufferHandle> m_touchedBuffers;   // 이번 프레임에 상태가 Common 에서 바뀐 버퍼

	// 이 패스의 기본 뷰포트 크기 
	uint32_t m_passWidth = 0;
	uint32_t m_passHeight = 0;
};

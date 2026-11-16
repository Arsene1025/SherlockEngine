#pragma once
#include "RHI/D3D11/D3D11Common.h"
#include "RHI/CommandList.h"
#include "RHI/BindingTypes.h"

class D3D11Device;
struct D3D11Texture;

// RHI::CommandList 의 D3D11 구현. ImmediateContext 의 얇은 래퍼라 "기록"이 곧 실행임.
class D3D11CommandList final : public RHI::CommandList
{
public:
	void Init(D3D11Device* device);   // D3D11Device::Init 이 호출함

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
	void CopyBuffer(BufferHandle dst, BufferHandle src) override;

	void WriteTimestamp(uint32_t slot) override;   // Device 의 쿼리 세트에 위임
	const Stats& GetStats() const override { return m_stats; }
	void ResetStats() override { m_stats = Stats{}; }
	void SetViewport(const Viewport& viewport) override;

private:
	void UnbindShaderResourcesOf(TextureHandle texture);   // VS·PS·CS 의 SRV 슬롯에서 풂
	void UnbindUavsOf(TextureHandle texture);              // CS 의 UAV 슬롯에서 풂
	// SRV 로 읽으려는 텍스처가 아직 UnorderedAccess 상태인지(또는 UAV 로 쓰려는 텍스처가 그 상태가 아닌지) 검사.
	// D3D11 은 배리어가 빠져도 화면이 맞게 나올 수 있어 D3D12 에서만 드러나는 실수를 여기서 미리 잡음.
	void CheckSrvState(const D3D11Texture* texture);
	void CheckUavState(const D3D11Texture* texture);
	static void ErrorOnce(bool& flag, const char* message);

	void ApplyViewport(const Viewport& viewport);

private:
	D3D11Device* m_device = nullptr;
	bool m_inPass = false;
	RenderPassDesc m_currentPass;
	Stats m_stats;

	// 현재 SRV 슬롯마다 어떤 텍스처가 바인딩돼 있는지 기록함. 렌더 패스가 그 텍스처를 타깃으로 잡기 전에
	// 풀어 주기 위함임. D3D11은 같은 리소스가 SRV와 RTV/DSV에 동시에 바인딩되면 경고한 뒤
	// 강제로 SRV를 NULL 로 만듦 — 우리가 먼저 풀어 두면 경고가 나지 않고 추적도 정확해짐.
	TextureHandle m_vsSrv[kMaxSrvRegister];
	TextureHandle m_psSrv[kMaxSrvRegister];
	// 컴퓨트도 같은 규칙: 같은 리소스가 CS 의 SRV 와 UAV 에 (또는 UAV 와 다른 스테이지의 SRV·RTV 에) 동시에
	// 바인딩되면 런타임이 한쪽을 NULL 로 만들고 경고함. Barrier 가 UAV 상태에 드나들 때 겹치는 쪽을 먼저 풂.
	TextureHandle m_csSrv[kMaxSrvRegister];
	TextureHandle m_csUav[kMaxUavRegister];
	bool m_computePipelineSet = false;

	// 이 패스의 기본 뷰포트 크기 
	uint32_t m_passWidth = 0;
	uint32_t m_passHeight = 0;
};

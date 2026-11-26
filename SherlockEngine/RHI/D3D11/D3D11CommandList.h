#pragma once
#include "RHI/D3D11/D3D11Common.h"
#include "RHI/CommandList.h"
#include "RHI/BindingTypes.h"

#include <vector>

class D3D11Device;
struct D3D11Texture;
struct D3D11Buffer;

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
	void DrawInstancedIndirect(BufferHandle args, uint32_t offsetBytes = 0) override;

	void SetComputePipeline(ComputePipelineHandle handle) override;
	void SetComputeResourceSet(uint32_t setIndex, ResourceSetHandle handle) override;
	void Dispatch(uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) override;
	void DispatchIndirect(BufferHandle args, uint32_t offsetBytes = 0) override;

	void Barrier(TextureHandle texture, ResourceState after) override;
	void UavBarrier(TextureHandle texture) override;
	void Barrier(BufferHandle buffer, ResourceState after) override;
	void UavBarrier(BufferHandle buffer) override;
	void CopyBuffer(BufferHandle dst, BufferHandle src) override;

	void WriteTimestamp(uint32_t slot) override;   // Device 의 쿼리 세트에 위임
	const Stats& GetStats() const override { return m_stats; }
	void ResetStats() override { m_stats = Stats{}; }
	void SetViewport(const Viewport& viewport) override;

	// 이번 프레임에 상태가 바뀐 버퍼를 Common 으로 되돌림. D3D11Device::EndFrame 이 부름.
	void ResetBufferStates();

private:
	void UnbindShaderResourcesOf(TextureHandle texture);   // VS·PS·CS 의 SRV 슬롯에서 풂
	void UnbindUavsOf(TextureHandle texture);              // CS 의 UAV 슬롯에서 풂
	// SRV 로 읽으려는 텍스처가 아직 UnorderedAccess 상태인지(또는 UAV 로 쓰려는 텍스처가 그 상태가 아닌지) 검사.
	// D3D11 은 배리어가 빠져도 화면이 맞게 나올 수 있어 D3D12 에서만 드러나는 실수를 여기서 미리 잡음.
	void CheckSrvState(const D3D11Texture* texture);
	void CheckUavState(const D3D11Texture* texture);
	// 버퍼판. 상태 추적 버퍼(UAV·IndirectArgs, IsStateTrackedBuffer)만 검사함 — SRV 로 읽을 때는 ShaderResource 상태여야 함 (D3D12 에서
	// Common 버퍼를 SRV 로 읽으면 암묵 승격되어 추적 상태와 실제 상태가 어긋남).
	void UnbindShaderResourcesOf(BufferHandle buffer);
	void UnbindUavsOf(BufferHandle buffer);
	void CheckSrvState(const D3D11Buffer* buffer);
	void CheckUavState(const D3D11Buffer* buffer);
	void TrackBufferState(BufferHandle handle, D3D11Buffer* buffer, ResourceState after);
	// *Indirect 의 인자 버퍼 검사 (IndirectArgs 플래그, 4 의 배수 오프셋, offset + argBytes ≤ size, 상태). 못 쓰면 nullptr.
	const D3D11Buffer* GetIndirectArgs(BufferHandle handle, uint32_t offsetBytes, uint32_t argBytes, const char* caller);
	static void ErrorOnce(bool& flag, const char* message);
	// 읽기 전용 깊이 패스에서 깊이 쓰기 PSO 로 그리려 하면 에러 로그(1회) 후 false — 드로우를 건너뜀.
	bool CheckReadOnlyDepthDraw(const char* caller);

	void ApplyViewport(const Viewport& viewport);

private:
	D3D11Device* m_device = nullptr;
	bool m_inPass = false;
	RenderPassDesc m_currentPass;
	Stats m_stats;

	// 현재 SRV 슬롯마다 어떤 텍스처가 바인딩돼 있는지 기록함. 렌더 패스가 그 텍스처를 타깃으로 잡기 전에
	// 풀어 주기 위함임. D3D11은 같은 리소스가 SRV와 RTV/DSV에 동시에 바인딩되면 경고한 뒤
	// 강제로 SRV를 NULL 로 만듦 — 우리가 먼저 풀어 두면 경고가 나지 않고 추적도 정확해짐.
	// 예외: 읽기 전용 DSV(DepthAttachment::readOnly) 는 같은 리소스의 SRV 와 함께 바인딩될 수 있으므로 풀지 않음.
	TextureHandle m_vsSrv[kMaxSrvRegister];
	TextureHandle m_psSrv[kMaxSrvRegister];
	// 컴퓨트도 같은 규칙: 같은 리소스가 CS 의 SRV 와 UAV 에 (또는 UAV 와 다른 스테이지의 SRV·RTV 에) 동시에
	// 바인딩되면 런타임이 한쪽(문서상 SRV 쪽)을 NULL 로 만들고 경고함. Barrier 가 UAV 상태에 드나들 때 겹치는 쪽을 먼저 풂.
	TextureHandle m_csSrv[kMaxSrvRegister];
	TextureHandle m_csUav[kMaxUavRegister];
	// 같은 레지스터의 버퍼판 (구조체 버퍼 SRV·UAV). 한 레지스터에는 텍스처 아니면 버퍼 하나만 기록됨 — 한쪽을
	// 꽂으면 다른 쪽 기록을 지움.
	BufferHandle m_vsSrvBuf[kMaxSrvRegister];
	BufferHandle m_psSrvBuf[kMaxSrvRegister];
	BufferHandle m_csSrvBuf[kMaxSrvRegister];
	BufferHandle m_csUavBuf[kMaxUavRegister];
	std::vector<BufferHandle> m_touchedBuffers;   // 이번 프레임에 상태가 Common 에서 바뀐 버퍼 (ResetBufferStates 가 되돌림)
	bool m_computePipelineSet = false;
	// 마지막 SetPipelineState 의 PSO 가 깊이를 쓰는지 (depthEnable && depthWrite). D3D11 의 PSO 는 패스를 넘어 남으므로
	// 패스 밖에서 설정한 PSO 도 드로우 때 검사함.
	bool m_pipelineDepthWrite = false;

	// 이 패스의 기본 뷰포트 크기
	uint32_t m_passWidth = 0;
	uint32_t m_passHeight = 0;
};

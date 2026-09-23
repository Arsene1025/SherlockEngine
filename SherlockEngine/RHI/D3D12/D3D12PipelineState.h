#pragma once
#include "RHI/D3D12/D3D12Common.h"
#include "RHI/PipelineTypes.h"
#include "RHI/BindingTypes.h"
#include <memory>
#include <unordered_map>
#include <vector>

class D3D12Device;

// BindingLayout 집합 → 루트 시그니처. 4단계가 "루트 시그니처의 D3D11 미리 그리기"라 부른 것이 여기서 진짜가 된다.
//
// 루트 파라미터 배치 (PSO Desc 의 bindingLayouts 순서대로):
//   셋마다  CB 슬롯 하나 = 루트 CBV 하나 (GPU 가상 주소. 드로우마다 다시 건다 — Dynamic 링 주소가 바뀌므로)
//           SRV 슬롯들   = 디스크립터 테이블 하나 (슬롯마다 range 1개, 셋의 영구 범위와 순서가 같다)
//           샘플러 슬롯들 = 디스크립터 테이블 하나
// 레지스터 space 는 전부 0 (SM 5.0 DXBC). 셋끼리 레지스터가 겹치지 않는 것은 4단계의 검증이 보장한다.
struct D3D12RootLayout
{
	struct SetBinding
	{
		BindingLayoutHandle layout;
		uint32_t cbvRoot[kMaxBindingSlots];   // 슬롯 → 루트 파라미터 인덱스 (CB 가 아니면 UINT32_MAX)
		uint32_t srvTableRoot = UINT32_MAX;
		uint32_t samplerTableRoot = UINT32_MAX;
	};

	ComPtr<ID3D12RootSignature> rootSignature;
	SetBinding sets[kMaxBindingSets];
	uint8_t setCount = 0;
	uint64_t key = 0;   // 레이아웃 핸들들의 해시
};

class D3D12PipelineState
{
public:
	bool Create(D3D12Device& device, const PipelineStateDesc& desc);

	ID3D12PipelineState* Get() const { return m_pso.Get(); }
	const D3D12RootLayout* GetRootLayout() const { return m_rootLayout.get(); }
	D3D12_PRIMITIVE_TOPOLOGY GetTopology() const { return m_topology; }
	const PipelineStateDesc& GetDesc() const { return m_desc; }
	ComPtr<ID3D12PipelineState> Detach() { return std::move(m_pso); }

private:
	PipelineStateDesc m_desc;
	ComPtr<ID3D12PipelineState> m_pso;
	std::shared_ptr<D3D12RootLayout> m_rootLayout;   // Device 의 캐시와 공유
	D3D12_PRIMITIVE_TOPOLOGY m_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

// Desc → PSO 캐시. D3D11 의 PipelineStateCache 와 같은 세대 규칙.
// 차이: Clear() 가 PSO 를 바로 지우지 않고 Device 의 지연 해제 목록에 넘긴다 (GPU 가 아직 쓰고 있을 수 있다).
class D3D12PipelineStateCache
{
public:
	PipelineHandle GetOrCreate(D3D12Device& device, const PipelineStateDesc& desc);
	const D3D12PipelineState* Get(PipelineHandle handle) const;
	size_t Count() const { return m_states.size(); }
	uint32_t GetGeneration() const { return m_generation; }
	void Clear(D3D12Device& device);

private:
	std::vector<std::unique_ptr<D3D12PipelineState>> m_states;
	std::unordered_map<PipelineStateDesc, uint32_t, PipelineStateDescHasher> m_lookup;
	uint32_t m_generation = 1;
};

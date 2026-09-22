#pragma once
#include "Graphics/D3D11/PipelineState.h"
#include <memory>
#include <unordered_map>
#include <vector>

class Device;

// Desc → PipelineState 캐시.
//
// 같은 Desc로 두 번 요청하면 같은 핸들이 돌아온다. 호출 코드는 매 프레임
// "지금 설정에 맞는 Desc"를 만들어 GetOrCreate를 불러도 되고, 첫 프레임 이후는
// 해시 조회 한 번으로 끝난다.
//
// PSO는 개별 삭제하지 않는다. 핸들의 generation은 항상 1이다.
// (셰이더 핫리로드로 PSO를 무효화하는 것은 4단계에서 Clear()로 처리한다.)
class PipelineStateCache
{
public:
	PipelineHandle GetOrCreate(Device& device, const PipelineStateDesc& desc);
	const PipelineState* Get(PipelineHandle handle) const;

	size_t Count() const { return m_states.size(); }
	void Clear();

private:
	std::vector<std::unique_ptr<PipelineState>> m_states;
	std::unordered_map<PipelineStateDesc, uint32_t, PipelineStateDescHasher> m_lookup;   // desc → index
};

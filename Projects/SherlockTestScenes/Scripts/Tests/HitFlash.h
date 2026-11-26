#pragma once
#include "Scene/Script.h"

// 실험용 스크립트 (Scripts\Tests\): 같은 오브젝트의 Highlight 로 피격 연출을 흉내 냄 (HighlightHitFlash.json).
// interval 초마다 "맞음" → 그 순간 tintAmount·flashIntensity 를 최대로 올리고 duration 초 동안 0 까지 선형으로 줄임.
// 가장자리 빛(rim)은 건드리지 않음 (상태 표시용으로 씬에 둔 값 그대로).
//
// 실제 게임에서는 interval 대신 "맞았을 때" 이벤트에서 m_elapsed = 0 으로 되돌리면 됨.
// 재생(Play) 중에만 돎. 편집 모드에서는 씬에 저장된 Highlight 값 그대로.
class HitFlash : public Script
{
public:
	const char* GetTypeName() const override;   // SHERLOCK_SCRIPT 가 정의함 (.cpp)
	void Reflect(PropertyVisitor& v) override;
	void Start() override;
	void Update(float dt) override;

	float interval = 1.0f;     // 맞는 주기 (초)
	float duration = 0.15f;    // 번쩍임이 사라지는 데 걸리는 시간 (초)
	float peakTint = 0.8f;     // 맞은 순간의 tintAmount
	float peakFlash = 2.0f;    // 맞은 순간의 flashIntensity

private:
	float m_elapsed = 0.0f;   // 마지막으로 맞은 뒤 지난 시간
};

#pragma once
#include "Scene/Script.h"

// 실험용 스크립트 (Scripts\Tests\): 같은 오브젝트를 디졸브로 사라지게 함 (DissolveOut.json).
// 오브젝트에 Dissolve 가 없으면 Start 에서 붙임 — 재생 중에 컴포넌트를 붙여도 된다는 예제 (미리 붙여 둘 필요 없음).
// delay 초 기다린 뒤 duration 초 동안 amount 를 0 → 1 로 올리고, 다 사라지면 hold 초 뒤 0 으로 되돌려 반복함.
// 재생(Play) 중에만 돎. 편집 모드에서는 씬에 저장된 값 그대로 (컴포넌트가 없으면 그대로 보임).
class DissolveOut : public Script
{
public:
	const char* GetTypeName() const override;   // SHERLOCK_SCRIPT 가 정의함 (.cpp)
	void Reflect(PropertyVisitor& v) override;
	void Start() override;
	void Update(float dt) override;

	float delay = 0.2f;      // 시작 전 대기 (초)
	float duration = 1.5f;   // 0 → 1 까지 걸리는 시간 (초)
	float hold = 0.5f;       // 다 사라진 뒤 되돌리기 전 대기 (초)

private:
	float m_elapsed = 0.0f;
};

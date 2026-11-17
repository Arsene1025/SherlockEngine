#pragma once
#include "Scene/Script.h"

// 실험용 스크립트 (Scripts\Tests\): 같은 오브젝트의 ParticleEmitter 를 interval 초마다 burstSeconds 동안만 뿜게 함
// (칼이 부딪힐 때 잠깐 튀는 불꽃 같은 "짧은 버스트" 예제, ParticleAll.json 의 SparkPulse).
//
// 이미터를 끄거나 오브젝트를 지우지 않고 spawnRate 만 burstRate ↔ 0 으로 바꿈. 이미터가 꺼지거나 사라지면 렌더러가
// GPU 버퍼째 지워서 이미 날아가던 입자가 그 자리에서 사라지기 때문 (Renderer::UpdateParticleEmitters).
// 한 번에 나오는 수 ≈ burstRate × burstSeconds. 이미터의 maxParticles 는 그보다 크게.
// 재생(Play) 중에만 돎. 편집 모드에서는 씬에 저장된 spawnRate 그대로 (예제 씬은 0 이라 안 보임).
class PulseEmitter : public Script
{
public:
	const char* GetTypeName() const override;   // SHERLOCK_SCRIPT 가 정의함 (.cpp)
	void Reflect(PropertyVisitor& v) override;
	void Start() override;
	void Update(float dt) override;

	float interval = 1.0f;        // 버스트 주기 (초)
	float burstSeconds = 0.08f;   // 한 번에 뿜는 시간 (초). 60fps 면 약 5 프레임
	float burstRate = 2500.0f;    // 뿜는 동안의 초당 생성 수

private:
	float m_elapsed = 0.0f;       // 이번 주기 안에서 지난 시간
};

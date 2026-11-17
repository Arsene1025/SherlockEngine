#include "pch.h"
#include "PulseEmitter.h"
#include "Scene/ParticleEmitter.h"
#include <cmath>

void PulseEmitter::Reflect(PropertyVisitor& v)
{
	v.Float("interval", interval, 0.05f);
	v.Float("burstSeconds", burstSeconds, 0.01f);
	v.Float("burstRate", burstRate, 10.0f);
}

void PulseEmitter::Start()
{
	//재생 시작하자마자 첫 버스트
	m_elapsed = 0.0f;
}

void PulseEmitter::Update(float dt)
{
	ParticleEmitter* emitter = GetOwner().GetBehaviour<ParticleEmitter>();
	if (emitter == nullptr) return;

	//주기 안에서의 시간. 주기를 넘으면 0부터 다시
	m_elapsed += dt;
	if (interval > 0.0f) m_elapsed = std::fmod(m_elapsed, interval);

	//주기 앞부분 burstSeconds 동안만 뿜고 나머지는 0
	//이미터는 계속 살아 있으니 이미 나간 입자는 수명이 다할 때까지 그대로 날아감
	emitter->spawnRate = m_elapsed < burstSeconds ? burstRate : 0.0f;
}

SHERLOCK_SCRIPT(PulseEmitter)

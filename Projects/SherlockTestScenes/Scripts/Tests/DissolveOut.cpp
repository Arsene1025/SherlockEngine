#include "pch.h"
#include "DissolveOut.h"
#include "Scene/Dissolve.h"
#include <algorithm>
#include <cmath>
#include <memory>

void DissolveOut::Reflect(PropertyVisitor& v)
{
	v.Float("delay", delay, 0.05f);
	v.Float("duration", duration, 0.05f);
	v.Float("hold", hold, 0.05f);
}

void DissolveOut::Start()
{
	m_elapsed = 0.0f;
	// Dissolve 가 없으면 지금 붙임. Scene::Update 는 인덱스로 순회하므로 순회 중에 추가해도 안전 (다음 칸에서 Start 가 불림)
	if (GetOwner().GetBehaviour<Dissolve>() == nullptr)
	{
		GetOwner().AddBehaviour(std::make_unique<Dissolve>());
	}
}

void DissolveOut::Update(float dt)
{
	Dissolve* dissolve = GetOwner().GetBehaviour<Dissolve>();
	if (dissolve == nullptr) return;

	// 대기 → 진행 → 유지를 한 주기로 반복
	m_elapsed += dt;
	const float period = delay + duration + hold;
	if (period > 0.0f)
	{
		m_elapsed = std::fmod(m_elapsed, period);
	}
	const float t = duration > 0.0f ? (m_elapsed - delay) / duration : 1.0f;
	dissolve->amount = std::clamp(t, 0.0f, 1.0f);
}

SHERLOCK_SCRIPT(DissolveOut)

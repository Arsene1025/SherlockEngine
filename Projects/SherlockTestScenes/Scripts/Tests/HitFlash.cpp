#include "pch.h"
#include "HitFlash.h"
#include "Scene/Highlight.h"
#include <algorithm>
#include <cmath>

void HitFlash::Reflect(PropertyVisitor& v)
{
	v.Float("interval", interval, 0.05f);
	v.Float("duration", duration, 0.01f);
	v.Float("peakTint", peakTint, 0.01f);
	v.Float("peakFlash", peakFlash, 0.05f);
}

void HitFlash::Start()
{
	//재생 시작하자마자 한 번 맞음
	m_elapsed = 0.0f;
}

void HitFlash::Update(float dt)
{
	Highlight* highlight = GetOwner().GetBehaviour<Highlight>();
	if (highlight == nullptr) return;

	//주기를 넘으면 다시 맞음
	m_elapsed += dt;
	if (interval > 0.0f)
	{
		m_elapsed = std::fmod(m_elapsed, interval);
	}

	//맞은 순간 1 → duration 뒤 0
	const float k = duration > 0.0f ? std::clamp(1.0f - m_elapsed / duration, 0.0f, 1.0f) : 0.0f;
	highlight->tintAmount = peakTint * k;
	highlight->flashIntensity = peakFlash * k;
}

SHERLOCK_SCRIPT(HitFlash)

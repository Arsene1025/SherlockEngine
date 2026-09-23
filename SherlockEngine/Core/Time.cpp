#include "pch.h"
#include "Core/Time.h"

void Time::Reset()
{
	m_timer.Reset();
	m_timer.Start();
	m_deltaTime = m_rawDeltaTime = 0.0f;
	m_totalTime = 0.0f;
	m_frameCount = 0;
	m_fps = 0.0f;
	m_fpsWindowTime = 0.0f;
	m_fpsWindowFrames = 0;
	m_accumulator = 0.0f;
	m_fixedStepsThisFrame = m_fixedStepsLastFrame = 0;
}

float Time::Tick()
{
	m_timer.Tick();
	m_rawDeltaTime = m_timer.DeltaTime();
	m_deltaTime = m_rawDeltaTime < 0.0f ? 0.0f : (m_rawDeltaTime > maxDeltaTime ? maxDeltaTime : m_rawDeltaTime);
	m_totalTime = m_timer.TotalTime();
	++m_frameCount;

	// FPS: 0.5초마다 창을 닫고 평균을 갱신한다. ImGui 의 Framerate 와 달리 우리 시계 기준이다.
	m_fpsWindowTime += m_rawDeltaTime;
	++m_fpsWindowFrames;
	if (m_fpsWindowTime >= 0.5f)
	{
		m_fps = static_cast<float>(m_fpsWindowFrames) / m_fpsWindowTime;
		m_fpsWindowTime = 0.0f;
		m_fpsWindowFrames = 0;
	}

	m_accumulator += m_deltaTime;
	m_fixedStepsLastFrame = m_fixedStepsThisFrame;
	m_fixedStepsThisFrame = 0;
	return m_deltaTime;
}

bool Time::ConsumeFixedStep()
{
	if (m_accumulator < m_fixedStep) return false;
	if (m_fixedStepsThisFrame >= maxFixedStepsPerFrame)
	{
		m_accumulator = 0.0f;   // 따라잡기 포기
		return false;
	}
	m_accumulator -= m_fixedStep;
	++m_fixedStepsThisFrame;
	return true;
}

#pragma once
#include <cstdint>
#include "Core/GameTimer.h"

// 프레임 시간 (10단계). GameTimer(QueryPerformanceCounter, Luna) 를 시계로 쓰고 그 위에
// 델타 클램프·프레임 카운터·FPS 평균·고정 스텝 누적기를 얹는다.
//
// 고정 스텝: 물리처럼 dt 가 일정해야 하는 갱신은 Tick 마다 누적된 시간을 fixedStep 단위로 소비한다
// (Fiedler, "Fix Your Timestep"). 프레임이 오래 걸리면 한 프레임에 여러 번, 짧으면 0번. 한 프레임에
// 최대 maxFixedStepsPerFrame 번까지만 — 그 이상은 따라잡기를 포기해 죽음의 나선을 막는다.
class Time
{
public:
	void Reset();            // 시계 시작 (Engine::Initialize)
	float Tick();            // 프레임마다 한 번. 클램프된 dt 를 돌려준다

	float GetDeltaTime() const { return m_deltaTime; }        // 클램프된 값 (≤ maxDeltaTime)
	float GetRawDeltaTime() const { return m_rawDeltaTime; }  // 실제 측정값
	float GetTotalTime() const { return m_totalTime; }        // Reset 이후 초 (일시정지 제외)
	uint64_t GetFrameCount() const { return m_frameCount; }
	float GetFps() const { return m_fps; }                    // 0.5초 창 평균
	float GetAverageFrameMs() const { return m_fps > 0.0f ? 1000.0f / m_fps : 0.0f; }

	// 고정 스텝. while (time.ConsumeFixedStep()) OnFixedUpdate(time.GetFixedStep());
	void SetFixedStep(float seconds) { m_fixedStep = seconds > 0.0f ? seconds : m_fixedStep; }
	float GetFixedStep() const { return m_fixedStep; }
	bool ConsumeFixedStep();
	uint32_t GetFixedStepsLastFrame() const { return m_fixedStepsLastFrame; }
	float GetInterpolationAlpha() const { return m_fixedStep > 0.0f ? m_accumulator / m_fixedStep : 0.0f; }

	float maxDeltaTime = 0.25f;           // 디버거 정지·창 이동 뒤의 거대한 dt 를 자른다
	uint32_t maxFixedStepsPerFrame = 8;

private:
	GameTimer m_timer;
	float m_deltaTime = 0.0f;
	float m_rawDeltaTime = 0.0f;
	float m_totalTime = 0.0f;
	uint64_t m_frameCount = 0;
	float m_fps = 0.0f;
	float m_fpsWindowTime = 0.0f;
	uint32_t m_fpsWindowFrames = 0;
	float m_fixedStep = 1.0f / 60.0f;
	float m_accumulator = 0.0f;
	uint32_t m_fixedStepsThisFrame = 0;
	uint32_t m_fixedStepsLastFrame = 0;
};

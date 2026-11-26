#pragma once
#include <cmath>

// 마우스 시선 회전을 시간에 대해 고르게 내보냄.
//
// 마우스 델타는 정수 픽셀이고 WM_MOUSEMOVE 는 마우스 보고 주기(125~1000 Hz)마다 몰려 들어옴. vsync 를 끄고 fps 가
// 보고 주기보다 높으면 프레임마다 받는 델타가 0, 1, 3, 0 … 처럼 튐. 그대로 회전에 쓰면 카메라 각속도가 프레임마다 튀고,
// 속도 버퍼와 모션 블러(기준 fps 보정이 높은 fps 에서 길이를 몇 배로 늘림)가 선명한 프레임과 번진 프레임을 번갈아 냄.
//
// 받은 회전량을 쌓아 두고 매 프레임 1 - exp(-dt / timeConstant) 만큼 꺼내는 단계를 두 번 거침. 총 회전량(감도)은
// fps 와 상관없이 그대로이고 각속도만 고르게 됨. timeConstant 가 초 단위라 fps 가 높을수록 반응이 fps 와 무관한 연속
// 2단 지수 (평균 지연 약 2 × timeConstant) 로 모이지만, 낮은 fps 에서는 한 프레임에 두 단계를 다 지나 조금 빨리 따라옴
// (회전을 받고 16.7 ms 뒤까지 나간 몫: 60 fps 0.77, 480 fps 0.65, 4800 fps 0.62).
// 델타에 dt 를 곱하는 방식은 감도가 fps 에 따라 바뀌므로 쓰지 않음.
// 한 단계(지연 15 ms)는 125 Hz 마우스·480 fps 에서 각속도가 평균의 0.7~1.3 배로 남았고, 두 단계(8 ms × 2)는 0.8~1.2 배
// (1000 Hz 마우스는 0.93~1.08 배). 시뮬레이션 기준.
struct LookSmoother
{
	float timeConstant = 0.008f;   // 초, 한 단계. 0 이하면 다듬지 않고 받은 만큼 바로 냄

	void Add(float x, float y)
	{
		m_stage1X += x;
		m_stage1Y += y;
	}

	// 이번 프레임에 적용할 회전량. dt ≤ 0 (시간 배율 0 등)이면 쌓인 것을 한 번에 냄 (회전이 멈추지 않게).
	void Consume(float dt, float& outX, float& outY)
	{
		const float k = (timeConstant > 0.0f && dt > 0.0f) ? 1.0f - std::exp(-dt / timeConstant) : 1.0f;
		const float midX = m_stage1X * k;
		const float midY = m_stage1Y * k;
		m_stage1X -= midX;
		m_stage1Y -= midY;
		m_stage2X += midX;
		m_stage2Y += midY;
		outX = m_stage2X * k;
		outY = m_stage2Y * k;
		m_stage2X -= outX;
		m_stage2Y -= outY;
		// 남은 양이 아주 작으면 마저 냄. 멈춘 카메라가 끝없이 미세하게 돌아 속도 버퍼가 0 이 안 되는 일을 막음.
		constexpr float kSnap = 1e-5f;   // 라디안
		if (std::fabs(m_stage1X) + std::fabs(m_stage2X) < kSnap && std::fabs(m_stage1Y) + std::fabs(m_stage2Y) < kSnap)
		{
			outX += m_stage1X + m_stage2X;
			outY += m_stage1Y + m_stage2Y;
			Reset();
		}
	}

	bool HasPending() const { return m_stage1X != 0.0f || m_stage1Y != 0.0f || m_stage2X != 0.0f || m_stage2Y != 0.0f; }
	void Reset() { m_stage1X = m_stage1Y = m_stage2X = m_stage2Y = 0.0f; }

private:
	float m_stage1X = 0.0f, m_stage1Y = 0.0f;   // 받았지만 아직 1단계를 안 지난 회전량
	float m_stage2X = 0.0f, m_stage2Y = 0.0f;   // 1단계를 지나 2단계에서 기다리는 회전량
};

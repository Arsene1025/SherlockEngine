#include "pch.h"
#include "Core/Input.h"
#include <cmath>
#include <cstring>
#include <Xinput.h>
#include "Core/Log.h"
#pragma comment(lib, "xinput.lib")   // XInput 1.4 (Windows 8+ 기본 포함)

void Input::KeyImpl(uint32_t vk, bool down)
{
	// 키 자동 반복(WM_KEYDOWN 연속)은 같은 값을 다시 쓰는 것이라 무해함.
	if (vk < kKeyCount)
	{
		m_keys[vk] = down;
	}
}

void Input::OnKeyDown(uint32_t vk)
{
	if (!m_simulated)
	{
		KeyImpl(vk, true);
	}
}

void Input::OnKeyUp(uint32_t vk)
{
	if (!m_simulated)
	{
		KeyImpl(vk, false);
	}
}

void Input::MouseButtonImpl(MouseButton button, bool down)
{
	const uint32_t index = static_cast<uint32_t>(button);
	if (index < kButtonCount)
	{
		m_mouse[index] = down;
	}
}

void Input::OnMouseButton(MouseButton button, bool down)
{
	if (!m_simulated)
	{
		MouseButtonImpl(button, down);
	}
}

void Input::OnMouseMove(int x, int y)
{
	if (!m_simulated)
	{
		MouseMoveImpl(x, y);
	}
}

void Input::SimulateKey(uint32_t vk, bool down)
{
	KeyImpl(vk, down);
}

void Input::SimulateMouseButton(MouseButton button, bool down)
{
	MouseButtonImpl(button, down);
}

void Input::SimulateMouseMove(int x, int y)
{
	MouseMoveImpl(x, y);
}

void Input::SimulateWheel(float delta)
{
	m_wheel += delta;
}

void Input::SimulatePad(uint16_t buttons, float leftX, float leftY)
{
	m_padSimulated = true;
	m_simulatedPad.connected = true;
	m_simulatedPad.buttons = buttons;
	m_simulatedPad.lx = leftX;
	m_simulatedPad.ly = leftY;
	m_pad = m_simulatedPad;   // 이번 프레임부터 (자동화는 PollGamepad 뒤에 부름 — 다음 Poll 까지 기다리면 한 프레임 늦음)
}

void Input::PollGamepad()
{
	if (m_padSimulated)
	{
		m_pad = m_simulatedPad;
		return;
	}
	if (!m_padWasConnected && m_padRetryFrames > 0)
	{
		--m_padRetryFrames;
		m_pad = PadState{};
		return;
	}
	XINPUT_STATE state = {};
	if (XInputGetState(0, &state) != ERROR_SUCCESS)
	{
		if (m_padWasConnected)
		{
			Log::Info("입력: 게임패드 0 연결 끊김");
		}
		m_padWasConnected = false;
		m_padRetryFrames = 60;
		m_pad = PadState{};
		return;
	}
	if (!m_padWasConnected)
	{
		Log::Info("입력: 게임패드 0 연결됨");
	}
	m_padWasConnected = true;
	const XINPUT_GAMEPAD& g = state.Gamepad;
	// 원형 데드존: 길이가 데드존보다 작으면 0, 넘으면 데드존~최대를 0~1 로 다시 늘림
	const auto stick = [](SHORT sx, SHORT sy, SHORT deadZone, float& outX, float& outY)
	{
		const float x = static_cast<float>(sx);
		const float y = static_cast<float>(sy);
		const float length = std::sqrt(x * x + y * y);
		if (length <= deadZone)
		{
			outX = outY = 0.0f;
			return;
		}
		const float scaled = (std::min)((length - deadZone) / (32767.0f - deadZone), 1.0f);
		outX = x / length * scaled;
		outY = y / length * scaled;
	};
	m_pad.connected = true;
	m_pad.buttons = g.wButtons;
	stick(g.sThumbLX, g.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, m_pad.lx, m_pad.ly);
	stick(g.sThumbRX, g.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, m_pad.rx, m_pad.ry);
	m_pad.lt = g.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? g.bLeftTrigger / 255.0f : 0.0f;
	m_pad.rt = g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? g.bRightTrigger / 255.0f : 0.0f;
}

void Input::MouseMoveImpl(int x, int y)
{
	if (m_hasPosition)
	{
		// 한 프레임에 WM_MOUSEMOVE가 여러 번 오면 델타를 누적함.
		m_dx += x - m_x;
		m_dy += y - m_y;
	}
	m_x = x;
	m_y = y;
	m_hasPosition = true;
}

void Input::OnMouseWheel(float delta)
{
	if (!m_simulated)
	{
		m_wheel += delta;
	}
}

void Input::OnFocusLost()
{
	if (m_simulated) return;   // 가짜 입력 중에는 창 포커스와 무관
	std::memset(m_keys, 0, sizeof(m_keys));
	std::memset(m_mouse, 0, sizeof(m_mouse));
	m_dx = 0;
	m_dy = 0;
	m_wheel = 0.0f;
	m_hasPosition = false;
}

void Input::OnCaptureLost()
{
	if (m_simulated) return;
	std::memset(m_mouse, 0, sizeof(m_mouse));
	m_dx = 0;
	m_dy = 0;
}

void Input::SetMousePositionSilently(int x, int y)
{
	if (m_simulated) return;   // 자동화가 정한 포인터를 커서 고정(마우스 보기)이 덮지 않게
	m_x = x;
	m_y = y;
	m_hasPosition = true;
}

void Input::EndFrame()
{
	std::memcpy(m_prevKeys, m_keys, sizeof(m_keys));
	std::memcpy(m_prevMouse, m_mouse, sizeof(m_mouse));
	m_prevPad = m_pad;
	m_dx = 0;
	m_dy = 0;
	m_wheel = 0.0f;
}

bool Input::IsKeyDown(uint32_t vk) const
{
	return vk < kKeyCount && m_keys[vk];
}

bool Input::IsKeyPressed(uint32_t vk) const
{
	return vk < kKeyCount && m_keys[vk] && !m_prevKeys[vk];
}

bool Input::IsKeyReleased(uint32_t vk) const
{
	return vk < kKeyCount && !m_keys[vk] && m_prevKeys[vk];
}

bool Input::IsMouseDown(MouseButton button) const
{
	const uint32_t index = static_cast<uint32_t>(button);
	return index < kButtonCount && m_mouse[index];
}

bool Input::IsMousePressed(MouseButton button) const
{
	const uint32_t index = static_cast<uint32_t>(button);
	return index < kButtonCount && m_mouse[index] && !m_prevMouse[index];
}

bool Input::IsMouseReleased(MouseButton button) const
{
	const uint32_t index = static_cast<uint32_t>(button);
	return index < kButtonCount && !m_mouse[index] && m_prevMouse[index];
}

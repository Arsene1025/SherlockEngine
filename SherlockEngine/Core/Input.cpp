#include "pch.h"
#include "Core/Input.h"
#include <cstring>

void Input::OnKeyDown(uint32_t vk)
{
	// 키 자동 반복(WM_KEYDOWN 연속)은 같은 값을 다시 쓰는 것이라 무해함.
	if (vk < kKeyCount) m_keys[vk] = true;
}

void Input::OnKeyUp(uint32_t vk)
{
	if (vk < kKeyCount) m_keys[vk] = false;
}

void Input::OnMouseButton(MouseButton button, bool down)
{
	const uint32_t index = static_cast<uint32_t>(button);
	if (index < kButtonCount) m_mouse[index] = down;
}

void Input::OnMouseMove(int x, int y)
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
	m_wheel += delta;
}

void Input::OnFocusLost()
{
	std::memset(m_keys, 0, sizeof(m_keys));
	std::memset(m_mouse, 0, sizeof(m_mouse));
	m_dx = 0;
	m_dy = 0;
	m_wheel = 0.0f;
	m_hasPosition = false;
}

void Input::OnCaptureLost()
{
	std::memset(m_mouse, 0, sizeof(m_mouse));
	m_dx = 0;
	m_dy = 0;
}

void Input::SetMousePositionSilently(int x, int y)
{
	m_x = x;
	m_y = y;
	m_hasPosition = true;
}

void Input::EndFrame()
{
	std::memcpy(m_prevKeys, m_keys, sizeof(m_keys));
	std::memcpy(m_prevMouse, m_mouse, sizeof(m_mouse));
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

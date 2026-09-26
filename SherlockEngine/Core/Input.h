#pragma once
#include <cstdint>

enum class MouseButton : uint8_t
{
	Left = 0,
	Right,
	Middle,
	Count
};

// 프레임 단위 입력 상태.
//
// Win32 메시지(WM_KEYDOWN 등)는 AppBase::MsgProc가 받아 On* 함수로 넣어 줌.
// 게임 코드는 Update(dt) 안에서 IsKeyDown / IsKeyPressed / GetMouseDeltaX 같은
// 질의만 함. 메시지는 프레임 사이에만 처리되므로(AppBase::Run의 PeekMessage
// 루프가 큐를 비운 뒤에만 한 프레임을 그림) 한 프레임 안에서 상태가 바뀌는
// 일은 없음.
//
// Pressed/Released는 이전 프레임과 비교해 만듦. EndFrame()이 프레임 끝에
// 현재 상태를 이전 상태로 옮기고 델타를 0으로 되돌림.
//
// ImGui가 입력을 쓰고 있는지(io.WantCaptureMouse/Keyboard)는 여기서 보지 않음.
// 공급은 무조건 하고 판단은 소비하는 쪽이 함. 공급을 막으면 UI 위에서 키를
// 뗐을 때 KeyUp을 놓쳐 키가 눌린 채로 고착됨.
class Input
{
public:
	// ---- Win32 메시지 공급 (AppBase::MsgProc에서만 호출) ----
	void OnKeyDown(uint32_t vk);
	void OnKeyUp(uint32_t vk);
	void OnMouseButton(MouseButton button, bool down);
	void OnMouseMove(int x, int y);        // 클라이언트 좌표. 캡처 중에는 음수도 옴.
	void OnMouseWheel(float delta);        // WHEEL_DELTA(120) 단위로 정규화된 값
	void OnFocusLost();                    // 모든 상태 초기화. Alt-Tab 후 키 고착 방지.
	void OnCaptureLost();                  // 마우스 버튼·델타만 초기화 (WM_CAPTURECHANGED)

	// 커서를 SetCursorPos로 옮긴 직후에 호출함. 현재 위치를 바꾸되 델타에는
	// 반영하지 않음. 이 호출이 없으면 커서를 중앙으로 되돌린 것 자체가 큰 마우스 이동으로 보임.
	void SetMousePositionSilently(int x, int y);

	// 프레임 끝. 현재 → 이전 복사, 델타·휠 초기화.
	void EndFrame();

	// ---- 질의 ----
	bool IsKeyDown(uint32_t vk) const;
	bool IsKeyPressed(uint32_t vk) const;    // 이번 프레임에 눌림
	bool IsKeyReleased(uint32_t vk) const;   // 이번 프레임에 뗌

	bool IsMouseDown(MouseButton button) const;
	bool IsMousePressed(MouseButton button) const;
	bool IsMouseReleased(MouseButton button) const;

	int GetMouseX() const { return m_x; }
	int GetMouseY() const { return m_y; }
	int GetMouseDeltaX() const { return m_dx; }
	int GetMouseDeltaY() const { return m_dy; }
	float GetWheelDelta() const { return m_wheel; }

private:
	static constexpr uint32_t kKeyCount = 256;
	static constexpr uint32_t kButtonCount = static_cast<uint32_t>(MouseButton::Count);

	bool m_keys[kKeyCount] = {};
	bool m_prevKeys[kKeyCount] = {};
	bool m_mouse[kButtonCount] = {};
	bool m_prevMouse[kButtonCount] = {};

	int m_x = 0;
	int m_y = 0;
	int m_dx = 0;
	int m_dy = 0;
	bool m_hasPosition = false;   // 첫 WM_MOUSEMOVE는 델타를 만들지 않음
	float m_wheel = 0.0f;
};

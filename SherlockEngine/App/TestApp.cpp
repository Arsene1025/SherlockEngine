#include "pch.h"
#include "App/TestApp.h"
#include <imgui.h>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

TestApp::TestApp()
{

}

bool TestApp::Initialize()
{
	if (!AppBase::Initialize()) return false;
	return true;
}

void TestApp::UpdateGUI()
{
	ImGui::Text("dt %.3f ms  total %.1f s", m_lastDt * 1000.0f, m_totalTime);

	ImGui::Separator();
	ImGui::Text("Camera  (WASD/QE move, RMB drag look, Shift fast)");
	const XMFLOAT3& p = camera.GetPosition();
	ImGui::Text("pos   %.2f  %.2f  %.2f", p.x, p.y, p.z);
	ImGui::Text("yaw   %.1f deg   pitch %.1f deg",
		XMConvertToDegrees(camera.GetYaw()), XMConvertToDegrees(camera.GetPitch()));
	ImGui::SliderFloat("Speed", &m_moveSpeed, 1.0f, 50.0f);
	ImGui::Text("F1 wireframe  F2 cull  F3 vsync");

	renderer.UpdateGUI();
}

void TestApp::Update(float dt)
{
	m_lastDt = dt;

	UpdateCamera(dt);

	// 렌더 설정 단축키. ImGui 텍스트 입력 중에는 무시한다.
	if (!ImGui::GetIO().WantCaptureKeyboard)
	{
		if (input.IsKeyPressed(VK_F1)) renderer.GetSettings().wireframe = !renderer.GetSettings().wireframe;
		if (input.IsKeyPressed(VK_F2)) renderer.GetSettings().cullBack = !renderer.GetSettings().cullBack;
		if (input.IsKeyPressed(VK_F3)) graphicsDevice.SetVSync(!graphicsDevice.IsVSync());
	}

	// 두 번째 구를 첫 구 주위로 공전시킨다. 각속도 1 rad/s 이므로 VSync를 켜고
	// 끄거나 프레임 속도가 달라져도 한 바퀴에 약 6.28초가 걸려야 한다.
	m_orbitAngle += dt;
	if (m_orbitAngle > XM_2PI) m_orbitAngle -= XM_2PI;
	const float orbitRadius = 9.0f;
	renderer.GetObject(1).GetTransform().SetPosition(
		-3.0f + orbitRadius * cosf(m_orbitAngle), 5.0f, orbitRadius * sinf(m_orbitAngle));
}

void TestApp::UpdateCamera(float dt)
{
	const ImGuiIO& io = ImGui::GetIO();

	// 회전 시작/종료. 시작 여부만 ImGui에 묻는다. 씬 위에서 시작한 드래그는
	// 커서가 UI 패널 위로 지나가도 계속 돌아야 하므로 latch로 둔다.
	if (!m_lookActive && input.IsMousePressed(MouseButton::Right) && !io.WantCaptureMouse)
	{
		BeginLook();
	}
	if (m_lookActive && input.IsMouseReleased(MouseButton::Right))
	{
		EndLook();
	}

	if (m_lookActive)
	{
		const int dx = input.GetMouseDeltaX();
		const int dy = input.GetMouseDeltaY();
		if (dx != 0 || dy != 0)
		{
			camera.Rotate(dx * m_lookSensitivity, dy * m_lookSensitivity);
		}

		// 커서를 시작점으로 되돌린다. 화면 가장자리에서 회전이 멈추지 않게.
		// SetCursorPos가 만드는 WM_MOUSEMOVE가 델타로 잡히지 않도록 Input에 알린다.
		SetCursorPos(m_lookAnchorScreen.x, m_lookAnchorScreen.y);
		POINT client = m_lookAnchorScreen;
		ScreenToClient(GetWindow(), &client);
		input.SetMousePositionSilently(client.x, client.y);
	}

	// 이동. 텍스트 필드에 입력 중이면 WASD가 UI로 가야 하므로 무시한다.
	if (!io.WantCaptureKeyboard)
	{
		float speed = m_moveSpeed * dt;
		if (input.IsKeyDown(VK_SHIFT)) speed *= 4.0f;

		float forward = 0.0f, right = 0.0f, up = 0.0f;
		if (input.IsKeyDown('W')) forward += speed;
		if (input.IsKeyDown('S')) forward -= speed;
		if (input.IsKeyDown('D')) right += speed;
		if (input.IsKeyDown('A')) right -= speed;
		if (input.IsKeyDown('E')) up += speed;
		if (input.IsKeyDown('Q')) up -= speed;

		if (forward != 0.0f || right != 0.0f || up != 0.0f)
		{
			camera.Move(forward, right, up);
		}
	}
}

void TestApp::BeginLook()
{
	if (m_lookActive) return;

	GetCursorPos(&m_lookAnchorScreen);
	// 창 밖으로 나가도 WM_MOUSEMOVE / WM_RBUTTONUP을 받는다.
	// SetCapture는 자기 자신에게도 WM_CAPTURECHANGED를 동기적으로 보낼 수 있으므로
	// (AppBase::MsgProc 참고) m_lookActive를 세우기 전에 부른다.
	SetCapture(GetWindow());

	m_lookActive = true;
	ShowCursor(FALSE);         // 카운터다. EndLook의 ShowCursor(TRUE)와 정확히 짝을 이룬다.

	// ImGui Win32 백엔드가 매 프레임 SetCursor로 커서 모양을 바꾸는 것을 막는다.
	ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
}

void TestApp::EndLook()
{
	if (!m_lookActive) return;
	m_lookActive = false;

	ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
	ShowCursor(TRUE);
	SetCursorPos(m_lookAnchorScreen.x, m_lookAnchorScreen.y);
	if (GetCapture() == GetWindow())
	{
		// ReleaseCapture는 WM_CAPTURECHANGED → OnFocusLost → EndLook을 다시 부른다.
		// m_lookActive가 이미 false이므로 위에서 바로 돌아온다.
		ReleaseCapture();
	}
}

void TestApp::OnFocusLost()
{
	// Alt-Tab 또는 캡처 상실. 커서가 숨겨진 채 남지 않게.
	EndLook();
}

void TestApp::Render()
{
	shaderClass.BindConstantBuffers();
	renderer.Render();
}

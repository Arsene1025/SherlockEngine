#include "pch.h"
#include "App/TestApp.h"
#include "App/DebugUI.h"
#include "Graphics/Mesh.h"
#include <imgui.h>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

TestApp::TestApp()
{

}

bool TestApp::Initialize()
{
	if (!AppBase::Initialize()) return false;

	BuildScene();
	graphicsDevice.SetClearColor(m_scene.clearColor[0], m_scene.clearColor[1], m_scene.clearColor[2], m_scene.clearColor[3]);
	return true;
}

void TestApp::BuildScene()
{
	// 3단계 완료 기준: 바닥 평면 위에 구·큐브가 여러 개 다른 변환으로 움직인다.
	Mesh* floor = m_scene.AddMesh(Mesh::CreatePlane(40.0f, 40.0f, 21, 21, XMFLOAT4(0.55f, 0.55f, 0.6f, 1.0f)));
	Mesh* sphere = m_scene.AddMesh(Mesh::CreateSphere(2.5f, 32, 16, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f)));
	Mesh* cube = m_scene.AddMesh(Mesh::CreateCube(3.0f, XMFLOAT4(0.9f, 0.5f, 0.3f, 1.0f)));
	Mesh* cylinder = m_scene.AddMesh(Mesh::CreateCylinder(1.5f, 1.0f, 4.0f, 24, 4, XMFLOAT4(0.4f, 0.8f, 0.5f, 1.0f)));

	m_scene.AddObject(floor, "Floor");

	m_scene.AddObject(sphere, "SphereCenter").GetTransform().SetPosition(0.0f, 2.5f, 0.0f);
	m_orbitSphere = m_scene.GetObjects().size();
	m_scene.AddObject(sphere, "SphereOrbit").GetTransform().SetPosition(8.0f, 2.5f, 0.0f);
	m_pulseSphere = m_scene.GetObjects().size();
	m_scene.AddObject(sphere, "SpherePulse").GetTransform().SetPosition(-10.0f, 2.5f, 8.0f);

	m_scene.AddObject(cube, "CubeStatic").GetTransform().SetPosition(10.0f, 1.5f, 10.0f);
	m_bobCube = m_scene.GetObjects().size();
	m_scene.AddObject(cube, "CubeBob").GetTransform().SetPosition(-8.0f, 3.0f, -8.0f);

	m_spinCylinder = m_scene.GetObjects().size();
	m_scene.AddObject(cylinder, "Cylinder").GetTransform().SetPosition(8.0f, 2.0f, -10.0f);

	// 기본 조명: 흰색 방향광 하나 (LightData 기본값).
	m_scene.GetLights().push_back(LightData{});

	// 첫 시점: 씬 전체가 보이도록 조금 뒤에서.
	camera.SetLookAt(XMFLOAT3(18.0f, 16.0f, -28.0f), XMFLOAT3(0.0f, 2.0f, 0.0f));
}

void TestApp::UpdateGUI()
{
	ImGui::Text("dt %.3f ms  total %.1f s  objects %zu", m_lastDt * 1000.0f, m_totalTime, m_scene.GetObjects().size());
	DebugUI::DrawCameraPanel(camera, m_moveSpeed);
	DebugUI::DrawRenderSettingsPanel(renderer, graphicsDevice);
	DebugUI::DrawLightPanel(m_scene);
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

	// ---- 애니메이션. 전부 dt 또는 누적 시간에 비례하므로 프레임 속도와 무관하다. ----
	std::vector<GameObject>& objects = m_scene.GetObjects();
	const float t = m_totalTime;

	// 공전: 각속도 1 rad/s → 한 바퀴 6.28초
	objects[m_orbitSphere].GetTransform().SetPosition(8.0f * cosf(t), 2.5f, 8.0f * sinf(t));
	// 상하 진동
	objects[m_bobCube].GetTransform().SetPosition(-8.0f, 3.0f + 1.5f * sinf(2.0f * t), -8.0f);
	// 자전 (Y축) + 살짝 기울임
	objects[m_spinCylinder].GetTransform().SetRotation(0.3f, t, 0.0f);
	// 크기 맥동
	const float pulse = 1.0f + 0.3f * sinf(3.0f * t);
	objects[m_pulseSphere].GetTransform().SetScale(pulse, pulse, pulse);
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
	renderer.Render(m_scene, camera, m_totalTime);
}

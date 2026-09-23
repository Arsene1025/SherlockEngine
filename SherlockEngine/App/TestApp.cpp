#include "pch.h"
#include "App/TestApp.h"
#include "App/DebugUI.h"
#include "Graphics/Mesh.h"
#include "Graphics/Model.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "Scene/SceneSerializer.h"
#include <imgui.h>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

TestApp::TestApp()
{

}

bool TestApp::OnInitialize()
{
	ParseAutomation();

	// 씬은 설정 engine.scene (실행 인자 --scene= 이 덮어쓴다). 기본은 데모 씬.
	// 11단계: 에디터 콜백 (메뉴·단축키 → 앱)
	m_editorCallbacks.switchScene = [this](int mode) { LoadSceneMode(static_cast<SceneMode>(mode)); };
	m_editorCallbacks.saveScene = [this](const std::wstring& path) { return SaveSceneFile(path); };
	m_editorCallbacks.loadScene = [this](const std::wstring& path) { return LoadSceneFile(path); };
	m_editorCallbacks.newScene = [this]() { BuildEmptyScene(); };
	m_editorCallbacks.addPrimitive = [this](int type) { AddPrimitive(type); };
	m_editorCallbacks.deleteObject = [this](int index) { DeleteObject(index); };

	SceneMode mode = SceneMode::Demo;
	const std::string option = GetConfig().GetString("engine.scene", "demo");
	if (option.size() > 5 && _stricmp(option.c_str() + option.size() - 5, ".json") == 0)
	{
		// 11단계: 저장한 씬 파일. 실패하면 데모 씬.
		std::wstring path(option.begin(), option.end());
		if (path.find(L':') == std::wstring::npos && path[0] != L'\\') path = Paths::GetSceneDir() + path;   // 상대 경로는 씬 폴더 기준
		m_editor.scenePath = path;
		if (LoadSceneFile(path)) return true;
	}
	if (_stricmp(option.c_str(), "helmet") == 0) mode = SceneMode::Helmet;
	else if (_stricmp(option.c_str(), "sponza") == 0) mode = SceneMode::Sponza;
	else if (_stricmp(option.c_str(), "demo") != 0) Log::Warn("알 수 없는 engine.scene 값 '%s'. 데모 씬으로 실행.", option.c_str());

	LoadSceneMode(mode);
	return true;
}

const char* TestApp::GetSceneName() const
{
	switch (m_sceneMode)
	{
	case SceneMode::Helmet: return "DamagedHelmet";
	case SceneMode::Sponza: return "Sponza";
	case SceneMode::File: return "File";
	default: return "Demo";
	}
}

void TestApp::LoadSceneMode(SceneMode mode)
{
	if (mode >= SceneMode::Count) mode = SceneMode::Demo;
	m_editor.ClearSelection();
	Scene& scene = GetEngine().GetScene();
	Renderer& renderer = GetEngine().GetRenderer();
	// Renderer 캐시는 메시·재질 포인터를 키로 쓴다. 씬을 비우기 전에 먼저 버린다 — 새로 만든 메시가
	// 우연히 같은 주소를 받으면 옛 GPU 버퍼가 그대로 쓰일 수 있다.
	GetEngine().GetRenderer().InvalidateScene(GetEngine().GetScene(), true);
	GetEngine().GetScene().Clear();
	m_modelStats = ModelStats{};
	m_sceneMode = mode;

	// 그림자·조명 설정을 씬에 맞게 되돌린다.
	RenderSettings& settings = renderer.GetSettings();
	settings.shadowOrthoSize = 60.0f;
	settings.shadowDistance = 40.0f;
	settings.shadowBias = 0.0015f;

	bool ok = true;
	if (mode == SceneMode::Demo) BuildDemoScene();
	else ok = BuildModelScene(mode);

	if (!ok)
	{
		Log::Error("씬 '%s' 를 만들지 못해 데모 씬으로 돌아간다.", GetSceneName());
		GetEngine().GetRenderer().InvalidateScene(GetEngine().GetScene(), true);
		GetEngine().GetScene().Clear();
		m_sceneMode = SceneMode::Demo;
		BuildDemoScene();
	}
	Log::Info("씬 전환: %s (오브젝트 %zu, 메시 %zu, 재질 %zu, 이미지 %zu)", GetSceneName(),
		scene.GetObjects().size(), scene.GetMeshes().size(), scene.GetMaterials().size(), scene.GetImageCount());
}

void TestApp::BuildDemoScene()
{
	Scene& scene = GetEngine().GetScene();
	Camera& camera = GetEngine().GetCamera();
	// 3단계 완료 기준: 바닥 평면 위에 구·큐브가 여러 개 다른 변환으로 움직인다.
	// 4단계 완료 기준: 같은 구 메시를 다른 Material로 여러 개 그린다.
	// 정점 색은 전부 흰색으로 두고 색은 Material이 정한다.
	const XMFLOAT4 white(1.0f, 1.0f, 1.0f, 1.0f);
	// 11단계: 출처(MeshSource)를 같이 넣어 씬을 JSON 으로 저장·복원할 수 있게.
	Mesh* floor = scene.AddMesh(Mesh::CreatePlane(40.0f, 40.0f, 21, 21, white), MeshSource::Plane(40.0f, 40.0f, 21, 21, white));
	Mesh* sphere = scene.AddMesh(Mesh::CreateSphere(2.5f, 32, 16, white), MeshSource::Sphere(2.5f, 32, 16, white));
	Mesh* cube = scene.AddMesh(Mesh::CreateCube(3.0f, white), MeshSource::Cube(3.0f, white));
	Mesh* cylinder = scene.AddMesh(Mesh::CreateCylinder(1.5f, 1.0f, 4.0f, 24, 4, white), MeshSource::Cylinder(1.5f, 1.0f, 4.0f, 24, 4, white));

	// 5단계: 텍스처. 바닥은 절차적 체커를 8×8 타일링 + 비등방 샘플러(비스듬히 보는 면의 밉맵 검증).
	// 6단계: 바닥에도 약한 노멀 맵.
	Material floorMat;
	floorMat.name = "Floor";
	floorMat.baseColor = XMFLOAT4(0.9f, 0.9f, 0.95f, 1.0f);
	floorMat.specularColor = XMFLOAT3(0.2f, 0.2f, 0.2f);
	floorMat.shininess = 8.0f;
	floorMat.albedoTexture = "builtin:checker";
	floorMat.uvScale = XMFLOAT2(8.0f, 8.0f);
	floorMat.sampler = SamplerPreset::AnisotropicWrap;
	floorMat.normalTexture = "bumps_normal.png";
	floorMat.normalStrength = 0.6f;

	// UV 검증용 파일 텍스처 (Assets/Textures/uv_checker.png, sRGB). 빨강=(0,0) 초록=u+ 파랑=v+.
	Material uvChecker;
	uvChecker.name = "UVChecker";
	uvChecker.albedoTexture = "uv_checker.png";
	uvChecker.specularColor = XMFLOAT3(0.3f, 0.3f, 0.3f);
	uvChecker.shininess = 32.0f;

	Material blueMatte;
	blueMatte.name = "BlueMatte";
	blueMatte.baseColor = XMFLOAT4(0.2f, 0.35f, 0.9f, 1.0f);
	blueMatte.specularColor = XMFLOAT3(0.1f, 0.1f, 0.1f);
	blueMatte.shininess = 4.0f;
	blueMatte.albedoTexture = "builtin:checker";
	blueMatte.uvScale = XMFLOAT2(2.0f, 1.0f);

	Material goldWire;
	goldWire.name = "GoldWire";
	goldWire.baseColor = XMFLOAT4(1.0f, 0.8f, 0.2f, 1.0f);
	goldWire.wireframe = true;

	// 6단계: 노멀 맵. 알베도 없이 색만 있는 재질에서 요철이 가장 잘 보인다.
	Material orange;
	orange.name = "Orange";
	orange.baseColor = XMFLOAT4(0.9f, 0.5f, 0.3f, 1.0f);
	orange.shininess = 32.0f;
	orange.normalTexture = "bumps_normal.png";
	orange.normalStrength = 1.5f;

	Material green;
	green.name = "Green";
	green.baseColor = XMFLOAT4(0.4f, 0.8f, 0.5f, 1.0f);
	green.specularColor = XMFLOAT3(0.6f, 0.6f, 0.6f);
	green.shininess = 24.0f;
	green.normalTexture = "bumps_normal.png";
	green.uvScale = XMFLOAT2(4.0f, 2.0f);

	// 감마 검증: sRGB 128 회색을 조명 없이 출력한다. 화면 픽셀이 128이어야 한다.
	Material grayCard;
	grayCard.name = "GrayCard128";
	grayCard.albedoTexture = "builtin:gray128";
	grayCard.unlit = true;

	const Material* floorMaterial = scene.AddMaterial(floorMat);
	const Material* uvMaterial = scene.AddMaterial(uvChecker);
	const Material* blueMaterial = scene.AddMaterial(blueMatte);
	const Material* goldMaterial = scene.AddMaterial(goldWire);
	const Material* orangeMaterial = scene.AddMaterial(orange);
	const Material* greenMaterial = scene.AddMaterial(green);
	const Material* grayMaterial = scene.AddMaterial(grayCard);

	scene.AddObject(floor, floorMaterial, "Floor");

	// 같은 구 메시, 다른 재질 세 개.
	scene.AddObject(sphere, uvMaterial, "SphereCenter").GetTransform().SetPosition(0.0f, 2.5f, 0.0f);
	m_orbitSphere = scene.GetObjects().size();
	scene.AddObject(sphere, blueMaterial, "SphereOrbit").GetTransform().SetPosition(8.0f, 2.5f, 0.0f);
	m_pulseSphere = scene.GetObjects().size();
	scene.AddObject(sphere, goldMaterial, "SpherePulse").GetTransform().SetPosition(-10.0f, 2.5f, 8.0f);

	scene.AddObject(cube, uvMaterial, "CubeStatic").GetTransform().SetPosition(10.0f, 1.5f, 10.0f);
	m_bobCube = scene.GetObjects().size();
	scene.AddObject(cube, orangeMaterial, "CubeBob").GetTransform().SetPosition(-8.0f, 3.0f, -8.0f);

	m_spinCylinder = scene.GetObjects().size();
	scene.AddObject(cylinder, greenMaterial, "Cylinder").GetTransform().SetPosition(8.0f, 2.0f, -10.0f);

	// 회색 카드: 카메라 가까이, 정면을 향하게.
	scene.AddObject(cube, grayMaterial, "GrayCard").GetTransform().SetPosition(14.0f, 1.5f, -14.0f);

	// 조명. 0번 방향광이 그림자를 만든다. 6단계: 점광·스포트라이트를 더해 여러 종류를 편집해 본다.
	scene.GetLights().push_back(LightData{});

	LightData pointLight;
	pointLight.type = static_cast<uint32_t>(LightType::Point);
	pointLight.position = XMFLOAT3(-6.0f, 5.0f, -4.0f);
	pointLight.color = XMFLOAT3(1.0f, 0.55f, 0.35f);
	pointLight.intensity = 3.0f;
	pointLight.range = 18.0f;
	scene.GetLights().push_back(pointLight);

	LightData spotLight;
	spotLight.type = static_cast<uint32_t>(LightType::Spot);
	spotLight.position = XMFLOAT3(10.0f, 10.0f, 2.0f);
	spotLight.direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
	spotLight.color = XMFLOAT3(0.4f, 0.6f, 1.0f);
	spotLight.intensity = 4.0f;
	spotLight.range = 25.0f;
	scene.GetLights().push_back(spotLight);

	scene.ambientColor = XMFLOAT3(0.12f, 0.12f, 0.12f);
	scene.clearColor[0] = 0.1f; scene.clearColor[1] = 0.1f; scene.clearColor[2] = 0.3f; scene.clearColor[3] = 1.0f;

	// 첫 시점: 씬 전체가 보이도록 조금 뒤에서.
	camera.SetLookAt(XMFLOAT3(18.0f, 16.0f, -28.0f), XMFLOAT3(0.0f, 2.0f, 0.0f));
}

bool TestApp::BuildModelScene(SceneMode mode)
{
	Scene& scene = GetEngine().GetScene();
	Renderer& renderer = GetEngine().GetRenderer();
	Camera& camera = GetEngine().GetCamera();
	// 9단계 완료 기준: glTF 모델이 텍스처·노멀맵·그림자와 함께 두 백엔드에서 같게 표시된다.
	const wchar_t* relative = mode == SceneMode::Helmet ? L"Models\\DamagedHelmet\\DamagedHelmet.glb" : L"Models\\Sponza\\Sponza.gltf";
	// 10단계: AssetManager 캐시. 두 번째 전환부터는 파싱하지 않는다 (패널의 hits 로 확인).
	const Model* cached = GetEngine().GetAssets().GetModel(relative);
	if (cached == nullptr) return false;
	const Model& model = *cached;
	const Bounds bounds = model.bounds;
	m_modelStats = model.stats;

	RenderSettings& settings = renderer.GetSettings();
	const XMFLOAT4 white(1.0f, 1.0f, 1.0f, 1.0f);

	if (mode == SceneMode::Helmet)
	{
		// 헬멧(지름 ≈ 2)을 바닥 위에 올려 그림자가 보이게. 바닥은 체커 + 약한 노멀 맵.
		Transform placement;
		placement.SetPosition(0.0f, -bounds.min.y + 0.05f, 0.0f);
		scene.AddModel(model, placement);

		Mesh* floor = scene.AddMesh(Mesh::CreatePlane(12.0f, 12.0f, 7, 7, white), MeshSource::Plane(12.0f, 12.0f, 7, 7, white));
		Material floorMat;
		floorMat.name = "Floor";
		floorMat.baseColor = XMFLOAT4(0.85f, 0.85f, 0.9f, 1.0f);
		floorMat.specularColor = XMFLOAT3(0.15f, 0.15f, 0.15f);
		floorMat.shininess = 8.0f;
		floorMat.albedoTexture = "builtin:checker";
		floorMat.uvScale = XMFLOAT2(3.0f, 3.0f);
		floorMat.sampler = SamplerPreset::AnisotropicWrap;
		scene.AddObject(floor, scene.AddMaterial(floorMat), "Floor");

		LightData sun;
		sun.direction = XMFLOAT3(0.45f, -0.8f, 0.4f);
		sun.intensity = 1.6f;
		scene.GetLights().push_back(sun);
		LightData fill;
		fill.type = static_cast<uint32_t>(LightType::Point);
		fill.position = XMFLOAT3(-3.0f, 2.5f, -2.0f);
		fill.color = XMFLOAT3(1.0f, 0.7f, 0.5f);
		fill.intensity = 1.5f;
		fill.range = 12.0f;
		scene.GetLights().push_back(fill);
		scene.ambientColor = XMFLOAT3(0.18f, 0.18f, 0.2f);
		scene.clearColor[0] = 0.12f; scene.clearColor[1] = 0.13f; scene.clearColor[2] = 0.18f; scene.clearColor[3] = 1.0f;

		settings.shadowOrthoSize = 10.0f;
		settings.shadowDistance = 12.0f;
		settings.shadowBias = 0.0008f;

		const float top = bounds.max.y - bounds.min.y + 0.05f;
		camera.SetLookAt(XMFLOAT3(2.6f, top * 0.9f, -3.2f), XMFLOAT3(0.0f, top * 0.5f, 0.0f));
		m_moveSpeed = 3.0f;
	}
	else
	{
		// Sponza: 건물 안에서 본다. 태양광은 가파르게 안뜰로 들어온다.
		scene.AddModel(model, Transform{});

		LightData sun;
		sun.direction = XMFLOAT3(0.25f, -1.0f, 0.12f);
		sun.intensity = 2.2f;
		scene.GetLights().push_back(sun);
		scene.ambientColor = XMFLOAT3(0.32f, 0.33f, 0.36f);
		scene.clearColor[0] = 0.35f; scene.clearColor[1] = 0.45f; scene.clearColor[2] = 0.6f; scene.clearColor[3] = 1.0f;

		const XMFLOAT3 center = bounds.Center();
		const XMFLOAT3 extent = bounds.Extent();
		const float largest = (std::max)((std::max)(extent.x, extent.y), extent.z);
		settings.shadowOrthoSize = largest * 2.2f;
		settings.shadowDistance = largest * 2.0f;
		settings.shadowBias = 0.0006f;

		// 긴 축(x)의 한쪽 끝 안쪽, 사람 눈높이보다 조금 위에서 반대편을 본다.
		// 안뜰을 대각선으로 본다: 긴 축(x) 한쪽 끝, 짧은 축(z) 한쪽 옆, 눈높이 ≈ 바닥 + 2.5.
		const XMFLOAT3 eye(center.x + extent.x * 0.45f, bounds.min.y + 2.6f, center.z + extent.z * 0.12f);
		const XMFLOAT3 target(center.x - extent.x * 0.9f, bounds.min.y + 4.0f, center.z - extent.z * 0.25f);
		camera.SetLookAt(eye, target);
		m_moveSpeed = (std::max)(2.0f, largest * 0.25f);
	}
	return true;
}

void TestApp::OnGUI()
{
	// 11단계: 에디터가 모든 창을 그린다 (도킹 공간·메뉴·씬 뷰·계층·인스펙터·설정·통계·콘솔).
	m_editor.cameraMoveSpeed = m_moveSpeed;
	m_editor.Draw(GetEngine(), GetSceneName(), m_modelStats, m_editorCallbacks);
	m_moveSpeed = m_editor.cameraMoveSpeed;
}

void TestApp::OnFixedUpdate(float fixedDt)
{
	// 고정 스텝 검증용. 물리가 들어오면 여기서 돈다.
	++m_fixedUpdates;
	m_fixedTime += fixedDt;
}

void TestApp::OnUpdate(float dt)
{
	Engine& engine = GetEngine();
	Scene& scene = engine.GetScene();
	Renderer& renderer = engine.GetRenderer();
	Input& input = engine.GetInput();
	const float totalTime = engine.GetTime().GetTotalTime();

	if (m_auto.active) RunAutomation();
	UpdateCamera(dt);

	// 렌더 설정 단축키. ImGui 텍스트 입력 중에는 무시한다.
	// 11단계: 화면 전체가 ImGui(도킹)라 WantCaptureKeyboard 는 늘 참이다. 텍스트 입력 중일 때만 막는다.
	if (!ImGui::GetIO().WantTextInput)
	{
		if (input.IsKeyPressed(VK_F1)) renderer.GetSettings().wireframe = !renderer.GetSettings().wireframe;
		if (input.IsKeyPressed(VK_F2)) renderer.GetSettings().cullBack = !renderer.GetSettings().cullBack;
		if (input.IsKeyPressed(VK_F3)) engine.GetDevice().SetVSync(!engine.GetDevice().IsVSync());
		if (input.IsKeyPressed(VK_F4)) renderer.ReloadShaders();
		if (input.IsKeyPressed(VK_F5)) renderer.GetSettings().srgbOutput = !renderer.GetSettings().srgbOutput;
		// F6: 샘플러 덮어쓰기 순환 (-1 재질대로 → 0..4 프리셋). 밉맵 유무 비교용.
		if (input.IsKeyPressed(VK_F6)) renderer.GetSettings().samplerOverride = (renderer.GetSettings().samplerOverride + 2) % 6 - 1;
		// 6단계
		if (input.IsKeyPressed(VK_F7)) m_lightOrbit = !m_lightOrbit;
		if (input.IsKeyPressed(VK_F8)) renderer.GetSettings().shadows = !renderer.GetSettings().shadows;
		if (input.IsKeyPressed(VK_F9)) renderer.GetSettings().normalMapping = !renderer.GetSettings().normalMapping;
		// 7단계: 결정적 화면 (애니메이션 t = 0, 광원 공전 정지)
		if (input.IsKeyPressed(VK_F10)) { m_freeze = !m_freeze; if (m_freeze) m_lightOrbit = false; }
		// 9단계: 씬 순환
		if (input.IsKeyPressed(VK_F11))
		{
			LoadSceneMode(static_cast<SceneMode>((static_cast<uint8_t>(m_sceneMode) + 1) % static_cast<uint8_t>(SceneMode::Count)));
		}
		// 10단계: 로그 콘솔 창
		if (input.IsKeyPressed(VK_F12)) m_editor.showConsole = !m_editor.showConsole;
	}

	// 방향광 0 을 Y축 둘레로 돌린다 (F7). 그림자가 따라오는지 본다.
	if (m_lightOrbit && !scene.GetLights().empty())
	{
		m_lightAngle += 0.6f * dt;
		scene.GetLights()[0].direction = XMFLOAT3(0.85f * cosf(m_lightAngle), -0.5f, 0.85f * sinf(m_lightAngle));
	}

	// ---- 애니메이션 (데모 씬만). 전부 dt 또는 누적 시간에 비례하므로 프레임 속도와 무관하다. ----
	if (m_sceneMode != SceneMode::Demo) return;
	if (m_pulseSphere >= scene.GetObjects().size() || m_spinCylinder >= scene.GetObjects().size()) return;   // 오브젝트를 지웠으면 애니메이션도 멈춘다
	std::vector<GameObject>& objects = scene.GetObjects();
	const float t = m_freeze ? 0.0f : totalTime;

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
	Input& input = GetEngine().GetInput();
	Camera& camera = GetEngine().GetCamera();

	// 회전 시작/종료. 시작 여부만 ImGui에 묻는다. 씬 위에서 시작한 드래그는
	// 커서가 UI 패널 위로 지나가도 계속 돌아야 하므로 latch로 둔다.
	// 11단계: 씬 뷰(ImGui 이미지) 위에서만 시작한다. 씬 뷰 안에서는 WantCaptureMouse 가 항상 참이므로 대신 hover 를 본다.
	if (!m_lookActive && input.IsMousePressed(MouseButton::Right) && m_editor.IsSceneViewHovered() && !m_editor.IsGizmoUsing())
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

	// 이동. 텍스트 필드에 입력 중이면 WASD가 UI로 가야 하므로 무시한다. 씬 뷰가 포커스/호버일 때만.
	if (!io.WantTextInput && (m_editor.IsSceneViewFocused() || m_editor.IsSceneViewHovered() || m_lookActive))
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

// ------------------------------------------------------------------ 11단계: 씬 저장/로드

bool TestApp::SaveSceneFile(const std::wstring& path)
{
	return SceneSerializer::Save(GetEngine().GetScene(), GetEngine().GetCamera(), path);
}

bool TestApp::LoadSceneFile(const std::wstring& path)
{
	Engine& engine = GetEngine();
	// Renderer 캐시는 포인터 키다. 씬을 비우기 전에 버린다 (9단계 LoadSceneMode 와 같은 순서).
	engine.GetRenderer().InvalidateScene(engine.GetScene(), true);
	m_editor.ClearSelection();
	if (!SceneSerializer::Load(engine.GetScene(), engine.GetCamera(), engine.GetAssets(), path))
	{
		Log::Error("씬 파일을 읽지 못해 데모 씬으로 돌아간다.");
		LoadSceneMode(SceneMode::Demo);
		return false;
	}
	m_sceneMode = SceneMode::File;
	m_modelStats = ModelStats{};
	// 데모 애니메이션 인덱스는 파일 씬에 맞지 않으므로 애니메이션은 File 모드에서 돌지 않는다 (OnUpdate 의 Demo 검사).
	Log::Info("씬 전환: File (오브젝트 %zu, 메시 %zu, 재질 %zu, 이미지 %zu)",
		engine.GetScene().GetObjects().size(), engine.GetScene().GetMeshes().size(), engine.GetScene().GetMaterials().size(), engine.GetScene().GetImageCount());
	return true;
}

// ------------------------------------------------------------------ 11단계: 자동 검증

void TestApp::ParseAutomation()
{
	const std::wstring exitAfter = GetCommandLineOption(L"exit-after");
	if (exitAfter.empty()) return;
	m_auto.active = true;
	m_auto.exitAfter = static_cast<uint64_t>(_wtoi64(exitAfter.c_str()));
	const std::wstring pick = GetCommandLineOption(L"pick");
	if (!pick.empty()) { m_auto.pick = swscanf_s(pick.c_str(), L"%f,%f", &m_auto.pickU, &m_auto.pickV) == 2; }
	const std::wstring position = GetCommandLineOption(L"set-position");
	if (!position.empty()) { m_auto.setPosition = swscanf_s(position.c_str(), L"%f,%f,%f", &m_auto.position.x, &m_auto.position.y, &m_auto.position.z) == 3; }
	auto resolve = [](const std::wstring& path) -> std::wstring
	{
		if (path.empty() || path.find(L':') != std::wstring::npos || path[0] == L'\\') return path;
		return Paths::GetSceneDir() + path;   // 상대 경로는 씬 폴더 기준
	};
	m_auto.savePath = resolve(GetCommandLineOption(L"save"));
	m_auto.loadPath = resolve(GetCommandLineOption(L"load"));
	m_auto.screenshotPath = resolve(GetCommandLineOption(L"screenshot"));
	m_auto.dumpObjects = !GetCommandLineOption(L"dump-objects").empty();
	m_auto.debugView = _wtoi(GetCommandLineOption(L"debug-view").c_str());
	m_auto.newScene = !GetCommandLineOption(L"new-scene").empty();
	m_auto.addPrimitive = GetCommandLineOption(L"add").empty() ? -1 : _wtoi(GetCommandLineOption(L"add").c_str());
	Log::Info("자동 검증: exit-after %llu, pick %d, set-position %d, save '%s', load '%s', screenshot '%s', dump %d",
		m_auto.exitAfter, m_auto.pick ? 1 : 0, m_auto.setPosition ? 1 : 0, Log::ToUtf8(m_auto.savePath.c_str()).c_str(),
		Log::ToUtf8(m_auto.loadPath.c_str()).c_str(), Log::ToUtf8(m_auto.screenshotPath.c_str()).c_str(), m_auto.dumpObjects ? 1 : 0);
}

void TestApp::RunAutomation()
{
	Engine& engine = GetEngine();
	Scene& scene = engine.GetScene();
	const uint64_t frame = engine.GetTime().GetFrameCount();

	if (frame == 2 && m_auto.debugView != 0) engine.GetRenderer().GetSettings().debugView = m_auto.debugView;
	if (frame == 3 && m_auto.newScene) BuildEmptyScene();
	if (frame == 4 && m_auto.addPrimitive >= 0) { AddPrimitive(m_auto.addPrimitive); m_editor.SetSelectedObject(static_cast<int>(scene.GetObjects().size()) - 1); }
	if (frame == 5 && m_auto.pick)
	{
		const int picked = m_editor.PickAt(scene, engine.GetCamera(), m_auto.pickU, m_auto.pickV);
		m_editor.SetSelectedObject(picked);
		Log::Info("자동 검증: pick (%.2f, %.2f) → %d %s", m_auto.pickU, m_auto.pickV, picked,
			picked >= 0 ? scene.GetObjects()[picked].GetName().c_str() : "(none)");
	}
	if (frame == 8 && m_auto.setPosition)
	{
		const int selected = m_editor.GetSelectedObject();
		if (selected >= 0 && selected < static_cast<int>(scene.GetObjects().size()))
		{
			scene.GetObjects()[selected].GetTransform().SetPosition(m_auto.position);
			Log::Info("자동 검증: '%s' 위치 → (%.2f, %.2f, %.2f)", scene.GetObjects()[selected].GetName().c_str(), m_auto.position.x, m_auto.position.y, m_auto.position.z);
		}
		else Log::Warn("자동 검증: 선택된 오브젝트가 없어 set-position 을 건너뜀");
	}
	if (frame == 12 && !m_auto.savePath.empty()) SaveSceneFile(m_auto.savePath);
	if (frame == 16 && !m_auto.loadPath.empty()) LoadSceneFile(m_auto.loadPath);
	if (frame == 20 && m_auto.dumpObjects)
	{
		for (const GameObject& object : scene.GetObjects())
		{
			const XMFLOAT3& p = object.GetTransform().GetPosition();
			Log::Info("자동 검증: 오브젝트 '%s' 위치 (%.2f, %.2f, %.2f)", object.GetName().c_str(), p.x, p.y, p.z);
		}
	}
	if (m_auto.exitAfter > 1 && frame == m_auto.exitAfter - 1 && !m_auto.screenshotPath.empty()) engine.RequestScreenshot(m_auto.screenshotPath);
	if (frame >= m_auto.exitAfter)
	{
		Log::Info("자동 검증: %llu 프레임 뒤 종료", frame);
		PostQuitMessage(0);
	}
}

// ------------------------------------------------------------------ 11단계: 새 씬 · 오브젝트 추가/삭제

void TestApp::BuildEmptyScene()
{
	Engine& engine = GetEngine();
	Scene& scene = engine.GetScene();
	engine.GetRenderer().InvalidateScene(scene, true);
	m_editor.ClearSelection();
	scene.Clear();
	m_sceneMode = SceneMode::File;
	m_modelStats = ModelStats{};

	// 바닥 하나, 방향광 하나. 나머지는 Hierarchy 의 + 버튼으로.
	const XMFLOAT4 white(1.0f, 1.0f, 1.0f, 1.0f);
	Mesh* floor = scene.AddMesh(Mesh::CreatePlane(40.0f, 40.0f, 21, 21, white), MeshSource::Plane(40.0f, 40.0f, 21, 21, white));
	Material floorMat;
	floorMat.name = "Floor";
	floorMat.baseColor = XMFLOAT4(0.9f, 0.9f, 0.95f, 1.0f);
	floorMat.specularColor = XMFLOAT3(0.2f, 0.2f, 0.2f);
	floorMat.shininess = 8.0f;
	floorMat.albedoTexture = "builtin:checker";
	floorMat.uvScale = XMFLOAT2(8.0f, 8.0f);
	floorMat.sampler = SamplerPreset::AnisotropicWrap;
	scene.AddObject(floor, scene.AddMaterial(floorMat), "Floor");
	scene.GetLights().push_back(LightData{});
	scene.ambientColor = XMFLOAT3(0.15f, 0.15f, 0.15f);
	scene.clearColor[0] = 0.1f; scene.clearColor[1] = 0.1f; scene.clearColor[2] = 0.3f; scene.clearColor[3] = 1.0f;

	RenderSettings& settings = engine.GetRenderer().GetSettings();
	settings.shadowOrthoSize = 60.0f;
	settings.shadowDistance = 40.0f;
	settings.shadowBias = 0.0015f;
	engine.GetCamera().SetLookAt(XMFLOAT3(18.0f, 16.0f, -28.0f), XMFLOAT3(0.0f, 2.0f, 0.0f));
	m_moveSpeed = 10.0f;
	Log::Info("씬 전환: New (바닥 1, 방향광 1)");
}

void TestApp::AddPrimitive(int type)
{
	Scene& scene = GetEngine().GetScene();
	const XMFLOAT4 white(1.0f, 1.0f, 1.0f, 1.0f);
	Mesh* mesh = nullptr;
	const char* name = "Object";
	float y = 0.0f;
	switch (type)
	{
	case 0: mesh = scene.AddMesh(Mesh::CreateSphere(1.5f, 32, 16, white), MeshSource::Sphere(1.5f, 32, 16, white)); name = "Sphere"; y = 1.5f; break;
	case 1: mesh = scene.AddMesh(Mesh::CreateCube(3.0f, white), MeshSource::Cube(3.0f, white)); name = "Cube"; y = 1.5f; break;
	case 2: mesh = scene.AddMesh(Mesh::CreateCylinder(1.5f, 1.0f, 4.0f, 24, 4, white), MeshSource::Cylinder(1.5f, 1.0f, 4.0f, 24, 4, white)); name = "Cylinder"; y = 2.0f; break;
	default: mesh = scene.AddMesh(Mesh::CreatePlane(10.0f, 10.0f, 2, 2, white), MeshSource::Plane(10.0f, 10.0f, 2, 2, white)); name = "Plane"; y = 0.01f; break;
	}

	// 기본 재질: 씬에 "Default" 가 있으면 재사용, 없으면 하나 만든다.
	const Material* material = nullptr;
	for (const auto& m : scene.GetMaterials()) if (m->name == "Default") material = m.get();
	if (material == nullptr)
	{
		Material defaultMat;
		defaultMat.name = "Default";
		defaultMat.baseColor = XMFLOAT4(0.75f, 0.75f, 0.8f, 1.0f);
		defaultMat.specularColor = XMFLOAT3(0.3f, 0.3f, 0.3f);
		defaultMat.shininess = 32.0f;
		material = scene.AddMaterial(defaultMat);
	}

	// 이름은 "Sphere 1", "Sphere 2" ... 처럼 번호를 붙인다.
	int count = 1;
	for (const GameObject& object : scene.GetObjects()) if (object.GetName().rfind(name, 0) == 0) ++count;
	const std::string uniqueName = std::string(name) + " " + std::to_string(count);
	scene.AddObject(mesh, material, uniqueName.c_str()).GetTransform().SetPosition(0.0f, y, 0.0f);
	if (m_sceneMode == SceneMode::Demo) m_sceneMode = SceneMode::File;   // 데모 애니메이션 인덱스와 어긋나지 않게
	Log::Info("오브젝트 추가: %s", uniqueName.c_str());
}

void TestApp::DeleteObject(int index)
{
	Scene& scene = GetEngine().GetScene();
	std::vector<GameObject>& objects = scene.GetObjects();
	if (index < 0 || index >= static_cast<int>(objects.size())) return;
	Log::Info("오브젝트 삭제: %s", objects[index].GetName().c_str());
	objects.erase(objects.begin() + index);   // 메시·재질은 씬이 계속 소유한다 (다른 오브젝트가 쓸 수 있다)
	if (m_sceneMode == SceneMode::Demo) m_sceneMode = SceneMode::File;
}

#include "pch.h"
#include "App/TestApp.h"
#include "App/DebugUI.h"
#include "Graphics/Mesh.h"
#include "Graphics/Model.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "Scene/SceneSerializer.h"
#include "Scene/CameraComponent.h"
#include "Game/Components/Rigidbody.h"
#include "App/ScriptCreator.h"
#include "App/GameBuilder.h"
#include "App/ProjectGenerator.h"
#include "App/ProjectLauncher.h"
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
	m_editorCallbacks.placeModel = [this](const std::wstring& path, const XMFLOAT3& position) { PlaceModel(path, position); };   // 11-B단계
	m_editorCallbacks.play = [this]() { StartPlay(); };   // 11-C단계
	m_editorCallbacks.pauseToggle = [this]() { TogglePause(); };
	m_editorCallbacks.stop = [this]() { StopPlay(); };
	m_editorCallbacks.stepFrame = [this]() { StepFrame(); };
	m_editorCallbacks.openProject = [this](const std::wstring& path) { return OpenProjectAndScene(path); };   // 11-E단계
	m_editorCallbacks.newProject = [this](const std::wstring& parent, const std::string& name, std::string& error) { return CreateProject(parent, name, error); };
	m_editorCallbacks.buildAndLaunchProjectEditor = [this]() { BuildAndLaunchProjectEditor(); };
	m_editorCallbacks.saveProject = [this]() { return GetProject().Save(); };

	// 11-E단계: 프로젝트가 있으면(AppBase 가 --project / exe 위쪽 / Projects\Sample 순으로 찾았다) 그 시작 씬. 없으면 engine.scene.
	SyncProjectInfo();
	m_editor.OnProjectChanged();
	if (GetProject().IsLoaded())
	{
		ProjectLauncher::AddRecent(GetProject().GetName(), GetProject().GetFilePath());
		if (!GetProject().startScene.empty())
		{
			std::wstring path = Paths::GetSceneDir() + std::wstring(GetProject().startScene.begin(), GetProject().startScene.end());
			m_editor.scenePath = path;
			if (GetCommandLineOption(L"scene").empty() && LoadSceneFile(path)) return true;   // --scene= 이 있으면 그것이 우선 (자동 검증)
		}
	}

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

	// 같은 구 메시, 다른 재질 세 개. 11-C단계: 움직임은 Game/Scripts 의 스크립트가 맡고 ▶ Play 에서만 돈다 (전에는 OnUpdate 하드코딩).
	// 예전의 Orbit·Bob·Pulse 같은 기성 컴포넌트는 없다 — 그런 움직임은 스크립트의 Update 에서 Transform 함수를 불러 직접 쓴다 (Rotator.cpp 참고).
	GameObject& center = scene.AddObject(sphere, uvMaterial, "SphereCenter");
	center.GetTransform().SetPosition(0.0f, 2.5f, 0.0f);
	if (auto* body = dynamic_cast<Rigidbody*>(center.AddBehaviour("Rigidbody"))) body->radius = 2.5f;   // 점프용
	center.AddBehaviour("PlayerController");   // 방향키 이동 + Space 점프 (Game/Scripts/PlayerController.cpp)
	scene.AddObject(sphere, blueMaterial, "SphereOrbit").GetTransform().SetPosition(8.0f, 2.5f, 0.0f);
	scene.AddObject(sphere, goldMaterial, "SpherePulse").GetTransform().SetPosition(-10.0f, 2.5f, 8.0f);

	GameObject& cubeStatic = scene.AddObject(cube, uvMaterial, "CubeStatic");
	cubeStatic.GetTransform().SetPosition(10.0f, 1.5f, 10.0f);
	if (auto* body = dynamic_cast<Rigidbody*>(cubeStatic.AddBehaviour("Rigidbody"))) body->initialVelocity = XMFLOAT3(0.0f, 9.0f, 0.0f);   // 위로 튀어 올랐다가 되튄다
	scene.AddObject(cube, orangeMaterial, "CubeBob").GetTransform().SetPosition(-8.0f, 3.0f, -8.0f);

	GameObject& cylinder0 = scene.AddObject(cylinder, greenMaterial, "Cylinder");
	cylinder0.GetTransform().SetPosition(8.0f, 2.0f, -10.0f);
	cylinder0.GetTransform().SetRotation(0.3f, 0.0f, 0.0f);   // 살짝 기울임
	cylinder0.AddBehaviour("Rotator");         // 예제 스크립트: 초당 60° 자전 (Game/Scripts/Rotator.cpp)

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

	// 11-C단계: 씬 안의 메인 카메라. 에디터에서 옮길 수 있고 재생 중 이 카메라로 본다 — 놓아 둔 자리 그대로, 보간 없이.
	// 추적 카메라를 원하면 Inspector 에서 FollowTarget(target = SphereCenter) 을 붙인다.
	AddCameraObject("Main Camera");
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
		AddCameraObject("Main Camera");   // 11-C단계
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
		AddCameraObject("Main Camera");   // 11-C단계
	}
	return true;
}

void TestApp::OnGUI()
{
	// 11단계: 에디터가 모든 창을 그린다 (도킹 공간·메뉴·씬 뷰·계층·인스펙터·설정·통계·콘솔).
	m_editor.cameraMoveSpeed = m_moveSpeed;
	SyncProjectInfo();   // 11-E단계
	m_editor.playState = static_cast<Editor::PlayState>(m_playState);   // 11-C단계: 표시용 상태, 시간 배율은 슬라이더에서 돌아온다
	m_editor.timeScale = m_timeScale;
	m_editor.Draw(GetEngine(), GetSceneName(), m_modelStats, m_editorCallbacks);
	m_moveSpeed = m_editor.cameraMoveSpeed;
	m_timeScale = m_editor.timeScale;
}

void TestApp::OnFixedUpdate(float fixedDt)
{
	// 고정 스텝 검증용. 11-C단계: 재생 중이면 컴포넌트의 FixedUpdate (Rigidbody 등).
	++m_fixedUpdates;
	m_fixedTime += fixedDt;
	if (m_playState == PlayState::Playing) GetEngine().GetScene().FixedUpdate(fixedDt * m_timeScale);
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
		// 7단계의 F10 "고정" 은 11-C단계에서 재생 일시정지가 됐다 (편집 중에는 아무것도 움직이지 않으므로).
		if (input.IsKeyPressed(VK_F10)) TogglePause();
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

	// ---- 11-C단계: 재생. 컴포넌트의 Update 는 재생 중에만, 시간 배율을 곱한 dt 로. Step 은 고정 스텝 한 번. ----
	(void)totalTime;
	if (m_playState == PlayState::Playing)
	{
		scene.Update(dt * m_timeScale);
	}
	else if (m_stepOnce)
	{
		m_stepOnce = false;
		const float step = engine.GetTime().GetFixedStep();
		scene.FixedUpdate(step);
		scene.Update(step);
	}
}

// ------------------------------------------------------------------ 11-E단계: 프로젝트

void TestApp::SyncProjectInfo()
{
	Editor::ProjectInfo& info = m_editor.project;
	const Project& project = GetProject();
	info.loaded = project.IsLoaded();
	info.name = project.GetName();
	info.startScene = project.startScene;
	info.root = project.GetRoot();
	info.solutionPath = project.GetSolutionPath();
	info.hasSolution = info.loaded && GetFileAttributesW(info.solutionPath.c_str()) != INVALID_FILE_ATTRIBUTES;
	// 이 exe 에 프로젝트 스크립트가 있나: 컴파일된 이름이 같거나, 컴파일된 이름이 없고(엔진 전용) 프로젝트에 스크립트가 없을 때
	const std::string compiled = GetCompiledProjectName();
	info.scriptsCompiledHere = !info.loaded || compiled == info.name;
	m_editor.projectStartScene = info.loaded ? &GetProject().startScene : nullptr;

	static std::string lastTitle;
	const std::string title = info.loaded ? "SherlockEditor - " + info.name : "SherlockEditor";
	if (title != lastTitle)
	{
		lastTitle = title;
		SetWindowTextW(GetWindow(), std::wstring(title.begin(), title.end()).c_str());
	}
}

bool TestApp::OpenProjectAndScene(const std::wstring& pathOrDir)
{
	if (m_playState != PlayState::Editing) StopPlay();
	if (!OpenProject(pathOrDir)) return false;
	SyncProjectInfo();
	m_editor.OnProjectChanged();
	ProjectLauncher::AddRecent(GetProject().GetName(), GetProject().GetFilePath());
	const std::string& start = GetProject().startScene;
	if (!start.empty())
	{
		const std::wstring path = Paths::GetSceneDir() + std::wstring(start.begin(), start.end());
		m_editor.scenePath = path;
		if (LoadSceneFile(path)) return true;
	}
	BuildEmptyScene();
	return true;
}

bool TestApp::CreateProject(const std::wstring& parentDir, const std::string& name, std::string& error)
{
	Project project;
	if (!Project::Create(parentDir, name, Paths::GetEngineRoot(), project, error)) return false;
	// 열고 나서 채운다: 예제 스크립트(ScriptCreator 가 프로젝트 Scripts\ 에 쓴다), 기본 씬, 솔루션
	if (!OpenProject(project.GetFilePath())) { error = "cannot open the new project"; return false; }
	SyncProjectInfo();
	m_editor.OnProjectChanged();
	std::string scriptError;
	if (!ScriptCreator::Create("Rotator", scriptError)) Log::Warn("새 프로젝트: 예제 스크립트 생성 실패: %s", scriptError.c_str());
	if (!ProjectGenerator::Generate(GetProject(), error)) Log::Warn("새 프로젝트: 솔루션 생성 실패: %s", error.c_str());   // 솔루션 없이도 프로젝트는 쓸 수 있다
	error.clear();
	BuildEmptyScene();
	const std::wstring scenePath = Paths::GetSceneDir() + L"Main.json";
	SaveSceneFile(scenePath);
	m_editor.scenePath = scenePath;
	ProjectLauncher::AddRecent(GetProject().GetName(), GetProject().GetFilePath());
	Log::Info("새 프로젝트 준비 완료: %s — Visual Studio 로 %s 를 열어 %sEditor 를 빌드하면 스크립트가 포함된 에디터가 된다",
		name.c_str(), Log::ToUtf8(GetProject().GetSolutionPath().c_str()).c_str(), name.c_str());
	return true;
}

void TestApp::BuildAndLaunchProjectEditor()
{
	const Project& project = GetProject();
	if (!project.IsLoaded()) return;
	std::string error;
	const std::wstring log = project.GetRoot() + L"Build\\msbuild-editor.log";
	if (!ProjectGenerator::RunMsBuild(project.GetSolutionPath(), project.GetEditorProjectName(), L"Debug", log, error))
	{
		Log::Error("프로젝트 에디터 빌드 실패: %s", error.c_str());
		return;
	}
	const std::wstring exe = project.GetBinariesDir(L"Debug") + project.GetEditorProjectName() + L".exe";
	const std::wstring arguments = L"--project=\"" + project.GetFilePath() + L"\"";
	const HINSTANCE result = ShellExecuteW(nullptr, L"open", exe.c_str(), arguments.c_str(), project.GetRoot().c_str(), SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) <= 32)
	{
		Log::Error("프로젝트 에디터 실행 실패: %s", Log::ToUtf8(exe.c_str()).c_str());
		return;
	}
	Log::Info("프로젝트 에디터로 전환: %s", Log::ToUtf8(exe.c_str()).c_str());
	PostQuitMessage(0);
}

// ------------------------------------------------------------------ 11-C단계: 재생

void TestApp::StartPlay()
{
	if (m_playState != PlayState::Editing) return;
	Engine& engine = GetEngine();
	Scene& scene = engine.GetScene();
	m_playSnapshot = SceneSerializer::SaveToString(scene, engine.GetCamera());   // 씬 + 카메라. Stop 이 되돌린다
	scene.BeginPlay(&engine.GetInput(), &engine.GetCamera());
	m_playState = PlayState::Playing;
	m_stepOnce = false;
	size_t behaviours = 0;
	for (const auto& object : scene.GetObjects()) behaviours += object->GetBehaviours().size();
	Log::Info("재생 시작: 오브젝트 %zu, 컴포넌트 %zu, 스냅샷 %zu 바이트", scene.GetObjects().size(), behaviours, m_playSnapshot.size());
}

void TestApp::StopPlay()
{
	if (m_playState == PlayState::Editing) return;
	Engine& engine = GetEngine();
	Scene& scene = engine.GetScene();
	scene.EndPlay();
	// 스냅샷 복원 = 씬 로드. Renderer 캐시는 메시·재질 포인터 키라 먼저 버린다 (텍스처는 그대로 — 다시 읽을 필요 없다).
	engine.GetRenderer().InvalidateScene(scene, false);
	const int selected = m_editor.GetSelectedObject();
	if (!SceneSerializer::LoadFromString(scene, engine.GetCamera(), engine.GetAssets(), m_playSnapshot, "play snapshot"))
	{
		Log::Error("재생 스냅샷을 되돌리지 못해 데모 씬으로 돌아간다.");
		LoadSceneMode(SceneMode::Demo);
	}
	m_editor.SetSelectedObject(selected < static_cast<int>(scene.GetObjects().size()) ? selected : -1);   // 오브젝트 순서는 같다
	m_playSnapshot.clear();
	m_playState = PlayState::Editing;
	m_stepOnce = false;
	Log::Info("재생 정지: 스냅샷 복원 (오브젝트 %zu)", scene.GetObjects().size());
}

void TestApp::TogglePause()
{
	if (m_playState == PlayState::Playing) { m_playState = PlayState::Paused; Log::Info("재생 일시정지"); }
	else if (m_playState == PlayState::Paused) { m_playState = PlayState::Playing; Log::Info("재생 재개"); }
}

void TestApp::StepFrame()
{
	if (m_playState == PlayState::Editing) return;
	m_playState = PlayState::Paused;
	m_stepOnce = true;
}

void TestApp::UpdateCamera(float dt)
{
	const ImGuiIO& io = ImGui::GetIO();
	Input& input = GetEngine().GetInput();
	Camera& camera = GetEngine().GetCamera();

	// 회전 시작/종료. 시작 여부만 ImGui에 묻는다. 씬 위에서 시작한 드래그는
	// 커서가 UI 패널 위로 지나가도 계속 돌아야 하므로 latch로 둔다.
	// 11단계: 씬 뷰(ImGui 이미지) 위에서만 시작한다. 씬 뷰 안에서는 WantCaptureMouse 가 항상 참이므로 대신 hover 를 본다.
	// 11-C단계: 재생 중 게임 카메라 컴포넌트가 카메라를 움직이면 에디터 컨트롤러는 손대지 않는다.
	if (m_playState != PlayState::Editing && GetEngine().GetScene().GetPlayContext().cameraDriven)
	{
		EndLook();
		return;
	}

	// 11-B단계: 에셋을 끌고 있는 동안은 시작하지 않는다 (드래그 중엔 hover 도 false 지만 의도를 명시한다).
	if (!m_lookActive && input.IsMousePressed(MouseButton::Right) && m_editor.IsSceneViewHovered() && !m_editor.IsGizmoUsing() && !m_editor.IsAssetDragActive())
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
	// 11-B단계
	m_auto.browseDir = GetCommandLineOption(L"browse");
	const std::wstring drop = GetCommandLineOption(L"drop");
	if (!drop.empty())
	{
		const size_t semicolon = drop.find(L';');
		m_auto.dropPath = drop.substr(0, semicolon);
		if (semicolon != std::wstring::npos) swscanf_s(drop.c_str() + semicolon + 1, L"%f,%f,%f", &m_auto.dropPosition.x, &m_auto.dropPosition.y, &m_auto.dropPosition.z);
	}
	const std::wstring dropUv = GetCommandLineOption(L"drop-uv");
	if (!dropUv.empty()) m_auto.dropUv = swscanf_s(dropUv.c_str(), L"%f,%f", &m_auto.dropU, &m_auto.dropV) == 2;
	m_auto.dropTexture = GetCommandLineOption(L"drop-texture");
	// 11-C단계
	m_auto.play = !GetCommandLineOption(L"play").empty();
	m_auto.stopAt = static_cast<uint64_t>(_wtoi64(GetCommandLineOption(L"stop-at").c_str()));
	m_auto.addComponent = Log::ToUtf8(GetCommandLineOption(L"add-component").c_str());
	m_auto.newScript = Log::ToUtf8(GetCommandLineOption(L"new-script").c_str());
	m_auto.buildGame = Log::ToUtf8(GetCommandLineOption(L"build-game").c_str());
	m_auto.buildScene = Log::ToUtf8(GetCommandLineOption(L"build-scene").c_str());
	m_auto.newProject = Log::ToUtf8(GetCommandLineOption(L"new-project").c_str());
	m_auto.buildProject = !GetCommandLineOption(L"build-project").empty();
	m_auto.launcher = !GetCommandLineOption(L"launcher").empty();
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
	if (frame == 2 && !m_auto.browseDir.empty()) { m_editor.GetContentBrowser().SetDirectory(m_auto.browseDir); m_editor.FocusContentBrowser(); }
	if (frame == 2 && !m_auto.newScript.empty())
	{
		std::string error;
		if (ScriptCreator::Create(m_auto.newScript, error)) Log::Info("자동 검증: 새 스크립트 %s 생성", m_auto.newScript.c_str());
		else Log::Error("자동 검증: 새 스크립트 실패: %s", error.c_str());
	}
	if (frame == 3 && m_auto.newScene) BuildEmptyScene();
	if (frame == 2 && m_auto.launcher) m_editor.ShowLauncher();
	if (frame == 2 && !m_auto.newProject.empty())
	{
		std::string error;
		const bool ok = CreateProject(ProjectLauncher::GetDefaultProjectsDir(), m_auto.newProject, error);
		Log::Info("자동 검증: new-project %s %s", m_auto.newProject.c_str(), ok ? "성공" : ("실패: " + error).c_str());
	}
	if (frame == 4 && m_auto.buildProject && GetProject().IsLoaded())
	{
		std::string error;
		const bool ok = ProjectGenerator::RunMsBuild(GetProject().GetSolutionPath(), GetProject().GetEditorProjectName(), L"Debug", GetProject().GetRoot() + L"Build\\msbuild-editor.log", error);
		Log::Info("자동 검증: build-project %s %s", ok ? "성공" : "실패", error.c_str());
	}
	if (frame == 3 && !m_auto.buildGame.empty())
	{
		GameBuilder::Options options;
		options.name = m_auto.buildGame;
		options.startScene = m_auto.buildScene.empty() ? (GetProject().startScene.empty() ? "demo_components.json" : GetProject().startScene) : m_auto.buildScene;
		options.projectName = GetProject().GetName();
		options.projectSolution = GetProject().GetSolutionPath();
		options.openFolder = false;
		const GameBuilder::Result result = GameBuilder::Build(options);
		Log::Info("자동 검증: build-game %s → %s", result.ok ? "성공" : "실패", result.message.c_str());
	}
	if (frame == 4 && m_auto.addPrimitive >= 0) { AddPrimitive(m_auto.addPrimitive); m_editor.SetSelectedObject(static_cast<int>(scene.GetObjects().size()) - 1); }
	if (frame == 5 && m_auto.pick)
	{
		const int picked = m_editor.PickAt(scene, engine.GetCamera(), m_auto.pickU, m_auto.pickV);
		m_editor.SetSelectedObject(picked);
		Log::Info("자동 검증: pick (%.2f, %.2f) → %d %s", m_auto.pickU, m_auto.pickV, picked,
			picked >= 0 ? scene.GetObjects()[picked]->GetName().c_str() : "(none)");
	}
	if (frame == 6 && !m_auto.dropPath.empty())
	{
		// 11-B단계: 씬 뷰 드롭과 같은 경로. --drop-uv 가 있으면 광선 히트점(오브젝트 AABB 또는 y=0 바닥).
		XMFLOAT3 position = m_auto.dropPosition;
		if (m_auto.dropUv)
		{
			const int hit = m_editor.RaycastScene(scene, engine.GetCamera(), m_auto.dropU, m_auto.dropV, position);
			Log::Info("자동 검증: drop-uv (%.2f, %.2f) → 히트 (%.2f, %.2f, %.2f) %s", m_auto.dropU, m_auto.dropV, position.x, position.y, position.z,
				hit >= 0 ? scene.GetObjects()[hit]->GetName().c_str() : "(ground)");
		}
		PlaceModel(m_auto.dropPath, position);
	}
	if (frame == 7 && !m_auto.dropTexture.empty())
	{
		const int selected = m_editor.GetSelectedObject();
		if (selected >= 0 && selected < static_cast<int>(scene.GetObjects().size()))
		{
			const GameObject& object = *scene.GetObjects()[selected];
			Material* material = nullptr;
			for (auto& m : scene.GetMaterials()) if (m.get() == object.GetMaterial()) material = m.get();
			if (material != nullptr)
			{
				material->albedoTexture = ContentBrowser::ToMaterialTextureName(m_auto.dropTexture);
				Log::Info("자동 검증: '%s' 재질 '%s' 알베도 → %s", object.GetName().c_str(), material->name.c_str(), material->albedoTexture.c_str());
			}
			else Log::Warn("자동 검증: 선택 오브젝트에 편집 가능한 재질이 없어 drop-texture 를 건너뜀");
		}
		else Log::Warn("자동 검증: 선택된 오브젝트가 없어 drop-texture 를 건너뜀");
	}
	if (frame == 9 && m_auto.play) StartPlay();   // 11-C단계
	if (frame == 10 && !m_auto.addComponent.empty())
	{
		const int selected = m_editor.GetSelectedObject();
		if (GameObject* object = scene.GetObject(selected < 0 ? SIZE_MAX : static_cast<size_t>(selected)))
		{
			Behaviour* added = object->AddBehaviour(m_auto.addComponent);
			Log::Info("자동 검증: '%s' 에 컴포넌트 %s %s", object->GetName().c_str(), m_auto.addComponent.c_str(), added ? "추가" : "추가 실패");
		}
		else Log::Warn("자동 검증: 선택된 오브젝트가 없어 add-component 를 건너뜀");
	}
	if (m_auto.stopAt != 0 && frame == m_auto.stopAt) StopPlay();
	if (frame == 8 && m_auto.setPosition)
	{
		const int selected = m_editor.GetSelectedObject();
		if (selected >= 0 && selected < static_cast<int>(scene.GetObjects().size()))
		{
			scene.GetObjects()[selected]->GetTransform().SetPosition(m_auto.position);
			Log::Info("자동 검증: '%s' 위치 → (%.2f, %.2f, %.2f)", scene.GetObjects()[selected]->GetName().c_str(), m_auto.position.x, m_auto.position.y, m_auto.position.z);
		}
		else Log::Warn("자동 검증: 선택된 오브젝트가 없어 set-position 을 건너뜀");
	}
	if (frame == 12 && !m_auto.savePath.empty()) SaveSceneFile(m_auto.savePath);
	if (frame == 16 && !m_auto.loadPath.empty()) LoadSceneFile(m_auto.loadPath);
	if (frame == 20 && m_auto.dumpObjects)
	{
		const Camera& camera = engine.GetCamera();
		const CameraComponent* active = scene.GetActiveCamera();
		Log::Info("자동 검증: 카메라 위치 (%.2f, %.2f, %.2f) yaw %.2f pitch %.2f, 활성 카메라 %s", camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z,
			camera.GetYaw(), camera.GetPitch(), active != nullptr ? active->GetOwner().GetName().c_str() : "(editor)");
		for (const auto& object : scene.GetObjects())
		{
			const XMFLOAT3& p = object->GetTransform().GetPosition();
			const char* albedo = object->GetMaterial() != nullptr ? object->GetMaterial()->albedoTexture.c_str() : "";
			std::string components;
			for (const auto& b : object->GetBehaviours()) components += std::string(components.empty() ? "" : ",") + b->GetTypeName();
			Log::Info("자동 검증: 오브젝트 '%s' 위치 (%.2f, %.2f, %.2f) albedo=%s components=[%s] %s", object->GetName().c_str(), p.x, p.y, p.z, albedo,
				components.c_str(), m_playState == PlayState::Editing ? "(editing)" : "(playing)");
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
	AddCameraObject("Main Camera");   // 11-C단계: 새 씬에도 카메라 하나
	Log::Info("씬 전환: New (바닥 1, 방향광 1, 카메라 1)");
}

GameObject& TestApp::AddCameraObject(const char* name)
{
	Scene& scene = GetEngine().GetScene();
	GameObject& object = scene.AddObject(nullptr, nullptr, name);   // 메시 없음 — 렌더러는 건너뛰고 에디터가 프러스텀 아이콘을 그린다
	if (auto* component = dynamic_cast<CameraComponent*>(object.AddBehaviour("CameraComponent"))) component->SetFromCamera(GetEngine().GetCamera());
	return object;
}

void TestApp::AddPrimitive(int type)
{
	Scene& scene = GetEngine().GetScene();
	if (type == 4)
	{
		// 11-C단계: 카메라 오브젝트. 지금 에디터가 보는 자리에 생긴다 ("Camera 1", "Camera 2" …).
		int count = 1;
		for (const auto& object : scene.GetObjects()) if (object->GetName().rfind("Camera ", 0) == 0) ++count;
		const std::string uniqueName = "Camera " + std::to_string(count);
		AddCameraObject(uniqueName.c_str());
		Log::Info("오브젝트 추가: %s (카메라)", uniqueName.c_str());
		return;
	}
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
	for (const auto& object : scene.GetObjects()) if (object->GetName().rfind(name, 0) == 0) ++count;
	const std::string uniqueName = std::string(name) + " " + std::to_string(count);
	scene.AddObject(mesh, material, uniqueName.c_str()).GetTransform().SetPosition(0.0f, y, 0.0f);
	// 끝에 추가하는 것은 데모 애니메이션 인덱스를 건드리지 않으므로 모드를 유지한다 (삭제만 File 로 바꾼다 — DeleteObject).
	Log::Info("오브젝트 추가: %s", uniqueName.c_str());
}

void TestApp::PlaceModel(const std::wstring& relativePath, const XMFLOAT3& position)
{
	Engine& engine = GetEngine();
	Scene& scene = engine.GetScene();
	// 첫 드롭은 동기 파싱이다 (Sponza 는 수 초). 두 번째부터는 AssetManager 캐시라 즉시.
	const Model* model = engine.GetAssets().GetModel(relativePath);
	if (model == nullptr || !model->IsValid())
	{
		Log::Error("모델을 놓지 못함: %s", Log::ToUtf8(relativePath.c_str()).c_str());
		return;
	}

	// 바닥면(bounds.min.y)이 놓은 점에 닿게. 헬멧 씬(BuildModelScene)의 규칙과 같다.
	Transform placement;
	placement.SetPosition(position.x, position.y - model->bounds.min.y, position.z);
	const size_t first = scene.GetObjects().size();
	const size_t created = scene.AddModel(*model, placement);   // 메시·재질·이미지를 복사한다 — 같은 모델을 두 번 놓으면 두 벌

	// 이름: "<파일명> N". 노드가 여럿이면 "<파일명> N/<노드명>". 번호는 AddPrimitive 처럼 접두어 개수로.
	std::string stem = Log::ToUtf8(relativePath.c_str());
	stem = stem.substr(stem.find_last_of("\\/") + 1);
	const size_t dot = stem.find_last_of('.');
	if (dot != std::string::npos && dot > 0) stem = stem.substr(0, dot);
	int count = 1;
	for (size_t i = 0; i < first; ++i) if (scene.GetObjects()[i]->GetName().rfind(stem + " ", 0) == 0) ++count;
	const std::string prefix = stem + " " + std::to_string(count);
	for (size_t i = first; i < first + created; ++i)
	{
		GameObject& object = *scene.GetObjects()[i];
		object.SetName(created == 1 ? prefix : prefix + "/" + object.GetName());
	}
	// 끝에 추가하는 것은 데모 애니메이션 인덱스(m_orbitSphere 등)를 건드리지 않으므로 모드를 바꾸지 않는다 — 데모 씬에 놓아도 구·큐브는 계속 움직인다.
	m_modelStats = model->stats;
	m_editor.SetSelectedObject(created > 0 ? static_cast<int>(first + created) - 1 : -1);
	Log::Info("모델 배치: %s ×%zu at (%.2f, %.2f, %.2f)", prefix.c_str(), created, position.x, position.y, position.z);
}

void TestApp::DeleteObject(int index)
{
	Scene& scene = GetEngine().GetScene();
	auto& objects = scene.GetObjects();
	if (index < 0 || index >= static_cast<int>(objects.size())) return;
	Log::Info("오브젝트 삭제: %s", objects[index]->GetName().c_str());
	scene.RemoveObject(static_cast<size_t>(index));   // 메시·재질은 씬이 계속 소유한다 (다른 오브젝트가 쓸 수 있다)
	if (m_sceneMode == SceneMode::Demo) m_sceneMode = SceneMode::File;
}

#include "pch.h"
#include "App/GameApp.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include "Scene/SceneSerializer.h"
#include "Scene/CameraComponent.h"

using namespace DirectX;

bool GameApp::OnInitialize()
{
	Engine& engine = GetEngine();
	const Config& config = GetConfig();

	// 창 제목은 CreateWindow 전에 GetWindowTitle 로 읽힘 — Initialize 순서상 이 함수는 LoadConfig 뒤에 불리므로 여기서 설정값으로 제목을 다시 지정함.
	const std::string title = config.GetString("game.title", "Sherlock Game");
	m_title = std::wstring(title.begin(), title.end());
	SetWindowTextW(GetWindow(), m_title.c_str());

	// 시작 씬: 프로젝트 파일(.sherlock)의 startScene, 없으면 engine.ini 의 [game] startScene 을 씀. 값은 Assets\Scenes\ 기준 파일명 또는 절대 경로임.
	std::string start = GetProject().IsLoaded() ? GetProject().startScene : "";
	if (start.empty()) start = config.GetString("game.startScene", "");
	if (start.empty())
	{
		Log::Error("게임: 시작 씬이 없다 (.sherlock 의 startScene 또는 engine.ini 의 [game] startScene). 에디터의 Build Game 이 채운다.");
		return false;
	}
	std::wstring path(start.begin(), start.end());
	for (wchar_t& c : path) if (c == L'/') c = L'\\';
	if (path.find(L':') == std::wstring::npos && path[0] != L'\\') path = Paths::GetAssetPath((L"Scenes\\" + path).c_str());

	engine.GetRenderer().InvalidateScene(engine.GetScene(), true);
	if (!SceneSerializer::Load(engine.GetScene(), engine.GetCamera(), engine.GetAssets(), path))
	{
		Log::Error("게임: 시작 씬을 읽지 못함: %s", Log::ToUtf8(path.c_str()).c_str());
		return false;
	}

	// 자동 검증 인자 (에디터와 같은 이름)
	const std::wstring exitAfter = GetCommandLineOption(L"exit-after");
	if (!exitAfter.empty()) m_exitAfter = static_cast<uint64_t>(_wtoi64(exitAfter.c_str()));
	m_screenshotPath = GetCommandLineOption(L"screenshot");
	m_dumpObjects = !GetCommandLineOption(L"dump-objects").empty();

	// 바로 재생. 카메라 오브젝트가 있으면 첫 Update 에서 그 시점으로 바뀜 (없으면 씬 파일의 카메라를 씀).
	Scene& scene = engine.GetScene();
	scene.BeginPlay(&engine.GetInput(), &engine.GetCamera());
	size_t behaviours = 0;
	for (const auto& object : scene.GetObjects()) behaviours += object->GetBehaviours().size();
	Log::Info("게임 시작: %s (오브젝트 %zu, 컴포넌트 %zu, 카메라 %s)", Log::ToUtf8(path.c_str()).c_str(), scene.GetObjects().size(), behaviours,
		scene.FindActiveCamera() != nullptr ? scene.FindActiveCamera()->GetOwner().GetName().c_str() : "(씬 파일의 카메라)");
	return true;
}

void GameApp::OnFixedUpdate(float fixedDt)
{
	GetEngine().GetScene().FixedUpdate(fixedDt);
}

void GameApp::OnUpdate(float dt)
{
	Engine& engine = GetEngine();
	Scene& scene = engine.GetScene();
	scene.Update(dt);

	if (engine.GetInput().IsKeyPressed(VK_ESCAPE)) PostQuitMessage(0);

	// 자동 검증
	if (m_exitAfter == 0) return;
	const uint64_t frame = engine.GetTime().GetFrameCount();
	if (frame == 20 && m_dumpObjects)
	{
		const Camera& camera = engine.GetCamera();
		Log::Info("게임 검증: 카메라 위치 (%.2f, %.2f, %.2f)", camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z);
		for (const auto& object : scene.GetObjects())
		{
			const XMFLOAT3& p = object->GetTransform().GetPosition();
			std::string components;
			for (const auto& b : object->GetBehaviours()) components += std::string(components.empty() ? "" : ",") + b->GetTypeName();
			Log::Info("게임 검증: 오브젝트 '%s' 위치 (%.2f, %.2f, %.2f) components=[%s]", object->GetName().c_str(), p.x, p.y, p.z, components.c_str());
		}
	}
	if (m_exitAfter > 1 && frame == m_exitAfter - 1 && !m_screenshotPath.empty()) engine.RequestScreenshot(m_screenshotPath);
	if (frame >= m_exitAfter)
	{
		Log::Info("게임 검증: %llu 프레임 뒤 종료", frame);
		PostQuitMessage(0);
	}
}

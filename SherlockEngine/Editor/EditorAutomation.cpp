#include "pch.h"
#include "Editor/EditorApp.h"
#include "Editor/DebugUI.h"
#include "Editor/ScriptCreator.h"
#include "Editor/GameBuilder.h"
#include "Editor/ProjectLauncher.h"
#include "Scene/CameraComponent.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include <format>   // C++20: 자동 검증 로그 서식

using namespace DirectX;   // 이 파일 안에서만

// 자동 검증 (EditorApp 의 멤버 함수를 파일만 나눠 구현함). 실행 인자를 읽고(ParseAutomation), 정해진 프레임에
// 에디터의 같은 코드 경로를 부름(RunAutomation). 인자 목록과 뜻은 Editor/EditorAutomation.h 에 있음.

void EditorApp::ParseAutomation()
{
	if (!GetCommandLineOption(L"post-stages").empty()) DebugUI::SetShowPostStages(true);   // --post-stages=1 이면 후처리 단계별 썸네일 창을 처음부터 켬
	const std::wstring exitAfter = GetCommandLineOption(L"exit-after");
	if (exitAfter.empty()) return;
	m_auto.active = true;
	m_auto.exitAfter = static_cast<uint64_t>(_wtoi64(exitAfter.c_str()));
	m_auto.selectName = Log::ToUtf8(GetCommandLineOption(L"select").c_str());
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
	m_auto.freezeCascadesAt = static_cast<uint64_t>(_wtoi64(GetCommandLineOption(L"freeze-cascades").c_str()));
	const std::wstring camLook = GetCommandLineOption(L"cam-look");
	if (!camLook.empty())
	{
		m_auto.camLook = swscanf_s(camLook.c_str(), L"%f,%f,%f;%f,%f,%f", &m_auto.camEye.x, &m_auto.camEye.y, &m_auto.camEye.z,
			&m_auto.camTarget.x, &m_auto.camTarget.y, &m_auto.camTarget.z) == 6;
	}
	m_auto.newScene = !GetCommandLineOption(L"new-scene").empty();
	m_auto.addPrimitive = GetCommandLineOption(L"add").empty() ? -1 : _wtoi(GetCommandLineOption(L"add").c_str());
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
	m_auto.play = !GetCommandLineOption(L"play").empty();
	m_auto.stopAt = static_cast<uint64_t>(_wtoi64(GetCommandLineOption(L"stop-at").c_str()));
	m_auto.addComponent = Log::ToUtf8(GetCommandLineOption(L"add-component").c_str());
	m_auto.newScript = Log::ToUtf8(GetCommandLineOption(L"new-script").c_str());
	m_auto.buildGame = Log::ToUtf8(GetCommandLineOption(L"build-game").c_str());
	m_auto.buildScene = Log::ToUtf8(GetCommandLineOption(L"build-scene").c_str());
	m_auto.newProject = Log::ToUtf8(GetCommandLineOption(L"new-project").c_str());
	m_auto.buildProject = !GetCommandLineOption(L"build-project").empty();
	m_auto.launcher = !GetCommandLineOption(L"launcher").empty();
	const std::wstring rotation = GetCommandLineOption(L"set-rotation");
	if (!rotation.empty()) { m_auto.setRotation = swscanf_s(rotation.c_str(), L"%f,%f,%f", &m_auto.rotationDeg.x, &m_auto.rotationDeg.y, &m_auto.rotationDeg.z) == 3; }
	const std::wstring setParent = GetCommandLineOption(L"set-parent");
	if (!setParent.empty())
	{
		const size_t semicolon = setParent.find(L';');
		m_auto.parentChild = Log::ToUtf8(setParent.substr(0, semicolon).c_str());
		m_auto.parentName = semicolon != std::wstring::npos ? Log::ToUtf8(setParent.substr(semicolon + 1).c_str()) : std::string();
	}
	m_auto.deleteName = Log::ToUtf8(GetCommandLineOption(L"delete").c_str());
	m_auto.duplicateName = Log::ToUtf8(GetCommandLineOption(L"duplicate").c_str());
	m_auto.makePrefabName = Log::ToUtf8(GetCommandLineOption(L"make-prefab").c_str());
	const std::wstring instantiate = GetCommandLineOption(L"instantiate");
	if (!instantiate.empty())
	{
		const size_t semicolon = instantiate.find(L';');
		m_auto.instantiatePath = instantiate.substr(0, semicolon);
		if (semicolon != std::wstring::npos) swscanf_s(instantiate.c_str() + semicolon + 1, L"%f,%f,%f", &m_auto.instantiatePosition.x, &m_auto.instantiatePosition.y, &m_auto.instantiatePosition.z);
	}
	m_auto.undoCount = _wtoi(GetCommandLineOption(L"undo").c_str());
	m_auto.redoCount = _wtoi(GetCommandLineOption(L"redo").c_str());
	m_auto.saveAfterPath = resolve(GetCommandLineOption(L"save-after"));
	m_auto.gesture = !GetCommandLineOption(L"gesture").empty();
	Log::Info("자동 검증: exit-after %llu, pick %d, set-position %d, save '%s', load '%s', screenshot '%s', dump %d",
		m_auto.exitAfter, m_auto.pick ? 1 : 0, m_auto.setPosition ? 1 : 0, Log::ToUtf8(m_auto.savePath.c_str()).c_str(),
		Log::ToUtf8(m_auto.loadPath.c_str()).c_str(), Log::ToUtf8(m_auto.screenshotPath.c_str()).c_str(), m_auto.dumpObjects ? 1 : 0);
}

void EditorApp::RunAutomation()
{
	Engine& engine = GetEngine();
	Scene& scene = engine.GetScene();
	const uint64_t frame = engine.GetTime().GetFrameCount();
	const EditorAutomation& a = m_auto;

	// 한 줄 = (프레임, 조건, 동작). 이번 프레임이 그 프레임이고 조건이 참이면 동작을 실행함. 줄의 순서가 곧
	// 실행 순서이므로 같은 프레임의 동작끼리는 위에 적힌 것이 먼저임. 새 인자를 추가할 때는 EditorAutomation 에
	// 필드, ParseAutomation 에 읽기, 여기에 한 줄.
	const auto at = [frame](uint64_t when, bool enabled, auto&& action)
	{
		if (enabled && frame == when) action();
	};
	const auto selectedObject = [&]() -> GameObject*
	{
		const int selected = m_editor.GetSelectedObject();
		return scene.GetObject(selected < 0 ? SIZE_MAX : static_cast<size_t>(selected));
	};

	// ---- 프레임이 인자로 정해지는 것 (CSM 디버그): 디버그 뷰(2) → 고정(N) → 카메라 이동(N+1, 고정이 없으면 3) → 종료 시 스크린샷 ----
	at(a.freezeCascadesAt, a.freezeCascadesAt != 0, [&] { engine.GetRenderer().SetCascadeFreeze(true, engine.GetCamera()); });
	at(a.freezeCascadesAt != 0 ? a.freezeCascadesAt + 1 : 3, a.camLook, [&] { engine.GetCamera().SetLookAt(a.camEye, a.camTarget); });

	// ---- 2: 설정·창 ----
	at(2, a.debugView != 0, [&] { engine.GetRenderer().GetSettings().debugView = a.debugView; });
	at(2, !a.browseDir.empty(), [&] { m_editor.GetContentBrowser().SetDirectory(a.browseDir); m_editor.FocusContentBrowser(); });
	at(2, !a.newScript.empty(), [&]
	{
		std::string error;
		if (ScriptCreator::Create(a.newScript, error)) Log::Info("자동 검증: 새 스크립트 %s 생성", a.newScript.c_str());
		else Log::Error("자동 검증: 새 스크립트 실패: %s", error.c_str());
	});
	at(2, a.launcher, [&] { m_editor.ShowLauncher(); });
	at(2, !a.newProject.empty(), [&]
	{
		std::string error;
		const bool ok = CreateProject(ProjectLauncher::GetDefaultProjectsDir(), a.newProject, error);
		Log::Info("자동 검증: new-project %s %s", a.newProject.c_str(), ok ? "성공" : ("실패: " + error).c_str());
	});

	// ---- 3: 새 씬, 게임 패키지 ----
	at(3, a.newScene, [&] { BuildEmptyScene(); });
	at(3, !a.buildGame.empty(), [&]
	{
		GameBuilder::Options options;
		options.name = a.buildGame;
		options.startScene = a.buildScene.empty() ? (GetProject().startScene.empty() ? "demo_components.json" : GetProject().startScene) : a.buildScene;
		options.projectName = GetProject().GetName();
		options.projectSolution = GetProject().GetSolutionPath();
		options.openFolder = false;
		options.releaseBuild = !GetCommandLineOption(L"build-release").empty();   // Release 패키지 검증
		const GameBuilder::Result result = GameBuilder::Build(options);
		Log::Info("자동 검증: build-game %s → %s", result.ok ? "성공" : "실패", result.message.c_str());
	});

	// ---- 4: 스크립트 빌드, 오브젝트 추가 ----
	at(4, a.buildProject && GetProject().IsLoaded(), [&]
	{
		BuildScripts();
		Log::Info("자동 검증: build-project → 스크립트 DLL %s (컴포넌트 %zu개)", GetScripts().IsLoaded() ? "올라옴" : "없음", GetScripts().GetTypeNames().size());
	});
	at(4, a.addPrimitive >= 0, [&]
	{
		RunCommand("Add object", [&]() { AddPrimitive(a.addPrimitive); m_editor.SetSelectedObject(static_cast<int>(scene.GetObjects().size()) - 1); });
	});

	// ---- 5: 선택 ----
	at(5, !a.selectName.empty(), [&]
	{
		int found = -1;
		for (size_t i = 0; i < scene.GetObjects().size() && found < 0; ++i)
			if (scene.GetObjects()[i]->GetName() == a.selectName) found = static_cast<int>(i);
		m_editor.SetSelectedObject(found);
		Log::Info("자동 검증: select '%s' → %d", a.selectName.c_str(), found);
	});
	at(5, a.pick, [&]
	{
		const int picked = m_editor.PickAt(scene, engine.GetCamera(), a.pickU, a.pickV);
		m_editor.SetSelectedObject(picked);
		Log::Info("자동 검증: pick (%.2f, %.2f) → %d %s", a.pickU, a.pickV, picked,
			picked >= 0 ? scene.GetObjects()[picked]->GetName().c_str() : "(none)");
	});

	// ---- 6: 모델 놓기, 삭제 — 씬 뷰 드롭·Delete 키가 부르는 함수를 그대로 부름 ----
	at(6, !a.dropPath.empty(), [&]
	{
		// 씬 뷰 드롭과 같은 경로. --drop-uv 가 있으면 광선 히트점(오브젝트 AABB 또는 y=0 바닥)에 놓음.
		XMFLOAT3 position = a.dropPosition;
		if (a.dropUv)
		{
			const int hit = m_editor.RaycastScene(scene, engine.GetCamera(), a.dropU, a.dropV, position);
			Log::Info("자동 검증: drop-uv (%.2f, %.2f) → 히트 (%.2f, %.2f, %.2f) %s", a.dropU, a.dropV, position.x, position.y, position.z,
				hit >= 0 ? scene.GetObjects()[hit]->GetName().c_str() : "(ground)");
		}
		RunCommand("Place model", [&]() { PlaceModel(a.dropPath, position); });
	});
	at(6, !a.deleteName.empty(), [&]
	{
		GameObject* target = scene.FindObject(a.deleteName);
		if (target == nullptr) Log::Warn("자동 검증: delete 대상 '%s' 를 찾지 못함", a.deleteName.c_str());
		else RunCommand("Delete object", [&]() { DeleteObject(static_cast<int>(scene.IndexOf(target))); });
	});

	// ---- 7: 텍스처 드롭, 재부모화 ----
	at(7, !a.dropTexture.empty(), [&]
	{
		const int selected = m_editor.GetSelectedObject();
		if (selected >= 0 && selected < static_cast<int>(scene.GetObjects().size()))
		{
			const GameObject& object = *scene.GetObjects()[selected];
			Material* material = nullptr;
			for (auto& m : scene.GetMaterials()) if (m.get() == object.GetMaterial()) material = m.get();
			if (material != nullptr)
			{
				RunCommand("Drop texture", [&]()
				{
					material->albedoTexture = ContentBrowser::ToMaterialTextureName(a.dropTexture);
					Log::Info("자동 검증: '%s' 재질 '%s' 알베도 → %s", object.GetName().c_str(), material->name.c_str(), material->albedoTexture.c_str());
				});
			}
			else Log::Warn("자동 검증: 선택 오브젝트에 편집 가능한 재질이 없어 drop-texture 를 건너뜀");
		}
		else Log::Warn("자동 검증: 선택된 오브젝트가 없어 drop-texture 를 건너뜀");
	});
	at(7, !a.parentChild.empty(), [&]
	{
		// --set-parent=자식;부모 (부모가 비어 있으면 루트로). 월드 자세는 유지됨 (Hierarchy 드래그와 같은 경로).
		// set-rotation(8 프레임) 보다 먼저 실행해, 부모를 붙인 뒤 부모를 돌리면 자식(카메라)이 따라 도는지
		// 검증할 수 있게 함.
		GameObject* child = scene.FindObject(a.parentChild);
		GameObject* parent = a.parentName.empty() ? nullptr : scene.FindObject(a.parentName);
		if (child == nullptr || (parent == nullptr && !a.parentName.empty()))
		{
			Log::Warn("자동 검증: set-parent 대상을 찾지 못함 ('%s' → '%s')", a.parentChild.c_str(), a.parentName.c_str());
			return;
		}
		bool ok = false;
		RunCommand("Reparent", [&]() { ok = child->SetParent(parent); });
		const XMFLOAT3 lp = child->GetTransform().GetPosition();
		const XMFLOAT3 wp = child->GetWorldPosition();
		Log::Info("자동 검증: set-parent '%s' → '%s' %s, 로컬 (%.2f, %.2f, %.2f) 월드 (%.2f, %.2f, %.2f)", a.parentChild.c_str(),
			parent != nullptr ? parent->GetName().c_str() : "(root)", ok ? "성공" : "거부(순환)", lp.x, lp.y, lp.z, wp.x, wp.y, wp.z);
	});

	// ---- 8: 선택 오브젝트의 위치·회전 ----
	at(8, a.setPosition, [&]
	{
		GameObject* object = selectedObject();
		if (object == nullptr)
		{
			Log::Warn("자동 검증: 선택된 오브젝트가 없어 set-position 을 건너뜀");
			return;
		}
		if (a.gesture)
		{
			m_forceBusy = true;   // 슬라이더를 잡고 끄는 중: 값은 바뀌지만 아직 단계로 기록되지 않음 (11 프레임째에 입력이 끝남)
			object->GetTransform().SetPosition(a.position);
		}
		else RunCommand("Move", [&]() { object->GetTransform().SetPosition(a.position); });
		Log::Info("자동 검증: '%s' 위치 → (%.2f, %.2f, %.2f)", object->GetName().c_str(), a.position.x, a.position.y, a.position.z);
	});
	at(8, a.setRotation, [&]
	{
		// 선택 오브젝트의 로컬 회전(도). roll 을 주고 카메라를 자식으로 붙이면 카메라 roll 상속을 검증할 수 있음.
		GameObject* object = selectedObject();
		if (object == nullptr)
		{
			Log::Warn("자동 검증: 선택된 오브젝트가 없어 set-rotation 을 건너뜀");
			return;
		}
		RunCommand("Rotate", [&]() { object->GetTransform().SetRotation(XMConvertToRadians(a.rotationDeg.x), XMConvertToRadians(a.rotationDeg.y), XMConvertToRadians(a.rotationDeg.z)); });
		Log::Info("자동 검증: '%s' 회전 → (%.1f, %.1f, %.1f) deg", object->GetName().c_str(), a.rotationDeg.x, a.rotationDeg.y, a.rotationDeg.z);
	});

	// ---- 9~10: 재생, 컴포넌트 추가 ----
	at(9, a.play, [&] { StartPlay(); });
	at(10, !a.addComponent.empty(), [&]
	{
		GameObject* object = selectedObject();
		if (object == nullptr)
		{
			Log::Warn("자동 검증: 선택된 오브젝트가 없어 add-component 를 건너뜀");
			return;
		}
		Behaviour* added = nullptr;
		RunCommand("Add component", [&]() { added = object->AddBehaviour(a.addComponent); });
		Log::Info("자동 검증: '%s' 에 컴포넌트 %s %s", object->GetName().c_str(), a.addComponent.c_str(), added ? "추가" : "추가 실패");
	});

	// ---- 11: 복제·프리팹 저장·프리팹 배치 — Ctrl+D·Save Prefab·씬 뷰 드롭이 부르는 함수를 그대로 부름. 끌기 흉내 끝 ----
	at(11, !a.duplicateName.empty(), [&]
	{
		GameObject* target = scene.FindObject(a.duplicateName);
		if (target == nullptr) Log::Warn("자동 검증: duplicate 대상 '%s' 를 찾지 못함", a.duplicateName.c_str());
		else RunCommand("Duplicate", [&]() { DuplicateObject(target->GetId()); });
	});
	at(11, !a.makePrefabName.empty(), [&]
	{
		GameObject* target = scene.FindObject(a.makePrefabName);
		if (target == nullptr) Log::Warn("자동 검증: make-prefab 대상 '%s' 를 찾지 못함", a.makePrefabName.c_str());
		else RunCommand("Save prefab", [&]() { SavePrefabOf(target->GetId()); });
	});
	at(11, !a.instantiatePath.empty(), [&] { RunCommand("Instantiate prefab", [&]() { InstantiatePrefabAt(a.instantiatePath, a.instantiatePosition); }); });
	at(11, a.gesture, [&] { m_forceBusy = false; });

	// ---- 12~20: 저장 → 되돌리기 → 다시 하기 → 불러오기 → 다시 저장 → 덤프 ----
	at(12, !a.savePath.empty(), [&] { SaveSceneFile(a.savePath); });
	at(14, a.undoCount > 0, [&] { for (int i = 0; i < a.undoCount; ++i) UndoRedo(true); });
	at(15, a.redoCount > 0, [&] { for (int i = 0; i < a.redoCount; ++i) UndoRedo(false); });
	at(16, !a.loadPath.empty(), [&] { LoadSceneFile(a.loadPath); });
	at(18, !a.saveAfterPath.empty(), [&] { SaveSceneFile(a.saveAfterPath); });
	at(20, a.dumpObjects, [&]
	{
		const Camera& camera = engine.GetCamera();
		const CameraComponent* active = scene.GetActiveCamera();
		Log::Info("자동 검증: 카메라 위치 (%.2f, %.2f, %.2f) yaw %.2f pitch %.2f roll %.2f, 활성 카메라 %s", camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z,
			camera.GetYaw(), camera.GetPitch(), camera.GetRoll(), active != nullptr ? active->GetOwner().GetName().c_str() : "(editor)");
		for (const auto& object : scene.GetObjects())
		{
			const XMFLOAT3& p = object->GetTransform().GetPosition();
			const char* albedo = object->GetMaterial() != nullptr ? object->GetMaterial()->albedoTexture.c_str() : "";
			std::string components;
			for (const auto& b : object->GetBehaviours()) components += std::string(components.empty() ? "" : ",") + b->GetTypeName();
			std::string parent;   // 부모가 있으면 이름과 월드 위치도 남김 (로컬 값과 비교용)
			if (object->GetParent() != nullptr)
			{
				const XMFLOAT3 wp = object->GetWorldPosition();
				parent = std::format(" parent='{}' world=({:.2f}, {:.2f}, {:.2f})", object->GetParent()->GetName(), wp.x, wp.y, wp.z);
			}
			Log::Info("자동 검증: 오브젝트 '%s' 위치 (%.2f, %.2f, %.2f) albedo=%s components=[%s]%s %s", object->GetName().c_str(), p.x, p.y, p.z, albedo,
				components.c_str(), parent.c_str(), m_playState == PlayState::Editing ? "(editing)" : "(playing)");
		}
	});

	// ---- 프레임이 인자로 정해지는 것: 정지, 스크린샷, 종료. 같은 프레임의 위 동작들이 끝난 뒤에 실행됨 ----
	at(a.stopAt, a.stopAt != 0, [&] { StopPlay(); });
	at(a.exitAfter - 1, a.exitAfter > 1 && !a.screenshotPath.empty(), [&] { engine.RequestScreenshot(a.screenshotPath); });
	if (frame >= a.exitAfter)
	{
		Log::Info("자동 검증: %llu 프레임 뒤 종료", frame);
		PostQuitMessage(0);
	}
}

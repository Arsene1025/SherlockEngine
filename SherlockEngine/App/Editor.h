#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <DirectXMath.h>
#include "App/ContentBrowser.h"   // 11-B단계
#include "App/GameBuilder.h"      // 11-D단계
#include "App/AppBase.h"          // 11-E단계: GetCompiledProjectName, Project
#include <vector>
#include "Scene/Camera.h"          // 11-C단계: 카메라 미리보기용 임시 카메라

class Engine;
class Scene;
class Camera;
class GameObject;
struct ModelStats;

// 에디터 UI (11단계). ImGui Dockspace 위의 창들: Scene(오프스크린 씬 뷰 + ImGuizmo + 클릭 선택), Hierarchy,
// Inspector(Transform·재질 편집), Render Settings(백엔드·PSO 통계·디버그 뷰·그림자), Lights, Materials, Stats, Console.
//
// 소유하지 않음: 씬·카메라·렌더러는 Engine 의 것이고, 씬 전환·저장·로드는 콜백으로 앱(TestApp)에 넘김.
// 패널은 데이터를 "편집"하는 쪽이지 "소유"하는 쪽이 아님 (3단계 DebugUI 의 규칙 그대로).
class Editor
{
public:
	struct Callbacks
	{
		std::function<void(int)> switchScene;                  // 0 데모, 1 헬멧, 2 Sponza
		std::function<bool(const std::wstring&)> saveScene;
		std::function<bool(const std::wstring&)> loadScene;
		std::function<void()> newScene;                       // 바닥만 있는 새 씬
		std::function<void(int)> addPrimitive;                 // 0 구, 1 큐브, 2 원기둥, 3 평면 — 원점 위에 기본 재질로 생성. 4 카메라 오브젝트 (11-C, 현재 에디터 시점 그대로)
		std::function<void(int)> deleteObject;                 // 오브젝트 인덱스
		// 11-B단계: 콘텐츠 브라우저. 모델(Assets 상대 경로)을 position 에 놓음 — 모델의 바닥면이 position.y 에 닿도록.
		std::function<void(const std::wstring& relativePath, const DirectX::XMFLOAT3& position)> placeModel;
		// 11-C단계: 재생. 상태(스냅샷·Scene::BeginPlay)는 앱이 갖고, 에디터는 버튼과 표시만 담당함.
		std::function<void()> play;        // Editing → Playing (씬 스냅샷)
		std::function<void()> pauseToggle; // Playing ↔ Paused
		std::function<void()> stop;        // → Editing (스냅샷 복원)
		std::function<void()> stepFrame;   // Paused 에서 한 프레임
		// 11-E단계: 프로젝트. Project 는 앱(AppBase)이 갖고, 에디터는 런처·메뉴만 담당함.
		std::function<bool(const std::wstring& pathOrDir)> openProject;
		std::function<bool(const std::wstring& parentDir, const std::string& name, std::string& error)> newProject;   // 폴더·솔루션·기본 씬을 만든 뒤 엶
		std::function<void()> buildAndLaunchProjectEditor;   // 프로젝트 솔루션의 에디터를 MSBuild 로 빌드하고 그 exe 로 갈아탐
		std::function<bool()> saveProject;                   // .sherlock 저장 (시작 씬 등)
	};

	struct ProjectInfo   // 앱이 매 프레임 넣어 주는 표시용 정보
	{
		bool loaded = false;
		std::string name;
		std::string startScene;
		std::wstring root;
		std::wstring solutionPath;
		bool scriptsCompiledHere = true;   // SHERLOCK_PROJECT_NAME == name 이면 참. 거짓이면 이 exe 에는 프로젝트 스크립트가 없음
		bool hasSolution = false;
	};

	enum class PlayState : uint8_t { Editing, Playing, Paused };

	Editor();

	// 프레임마다 ImGui::NewFrame 뒤에 호출. 도킹 레이아웃·메뉴·모든 창을 그림.
	void Draw(Engine& engine, const char* sceneName, const ModelStats& modelStats, const Callbacks& callbacks);

	// ---- 씬 뷰 상태 (앱의 카메라 컨트롤러가 참조함) ----
	bool IsSceneViewHovered() const { return m_sceneHovered; }
	bool IsSceneViewFocused() const { return m_sceneFocused; }
	bool IsGizmoUsing() const { return m_gizmoUsing; }
	uint32_t GetSceneViewWidth() const { return m_sceneWidth; }
	uint32_t GetSceneViewHeight() const { return m_sceneHeight; }

	int GetSelectedObject() const { return m_selected; }
	void SetSelectedObject(int index) { m_selected = index; }
	void ClearSelection() { m_selected = -1; }
	// 자동 검증용: 클릭과 같은 코드 경로로 (u, v) 픽셀의 오브젝트를 고름.
	int PickAt(Scene& scene, const Camera& camera, float u, float v) const { return Pick(scene, camera, u, v); }
	// 11-B단계: 드롭 위치를 구함. (u, v) 광선이 오브젝트 AABB 에 맞으면 그 교점(반환 = 오브젝트 인덱스), 아니면 y = 0 바닥면과의 교점(반환 −1).
	// 둘 다 아니면(하늘을 보며 놓은 경우) 광선을 따라 10 단위 떨어진 점. 드롭 코드와 --drop-uv 자동 검증이 함께 씀.
	int RaycastScene(Scene& scene, const Camera& camera, float u, float v, DirectX::XMFLOAT3& outPoint) const;

	// 11-B단계: 콘텐츠 브라우저의 에셋을 끌고 있는 동안 참. 이때 앱의 카메라 컨트롤러는 RMB 룩을 시작하지 않음.
	bool IsAssetDragActive() const { return m_assetDragActive; }
	ContentBrowser& GetContentBrowser() { return m_contentBrowser; }
	void FocusContentBrowser() { showContentBrowser = true; m_focusContentBrowserFrames = 3; }   // 도킹 탭을 앞으로 가져옴 (자동 검증 스크린샷용)

	bool showConsole = true;
	bool showContentBrowser = true;
	PlayState playState = PlayState::Editing;   // 11-C단계: 앱이 매 프레임 넣어 줌 (표시용)
	float timeScale = 1.0f;                       // 씬 뷰 툴바의 슬라이더. 앱이 읽어 dt 에 곱함
	ProjectInfo project;                          // 11-E단계: 앱이 매 프레임 넣어 줌
	std::string* projectStartScene = nullptr;     // Project Settings 팝업이 편집하는 시작 씬 (앱의 Project::startScene)
	void OnProjectChanged();                      // 앱이 프로젝트를 열거나 바꾼 뒤 호출: 브라우저·선택·경로 초기화
	void ShowLauncher() { m_openLauncher = true; }
	float cameraMoveSpeed = 10.0f;   // Render Settings 의 카메라 패널이 편집하고 앱의 컨트롤러가 읽음
	std::wstring scenePath;   // 저장/로드 경로 (메뉴의 텍스트 필드)

private:
	void DrawMenuBar(Engine& engine, const Callbacks& callbacks);
	void BuildDefaultLayout(uint32_t dockspaceId);
	void DrawSceneView(Engine& engine, const Callbacks& callbacks);
	void DrawHierarchy(Engine& engine, const Callbacks& callbacks);
	void DrawInspector(Engine& engine);
	void DrawRenderSettings(Engine& engine);
	void DrawStats(Engine& engine, const char* sceneName, const ModelStats& modelStats);
	void DrawGizmo(Engine& engine, float x, float y, float width, float height);
	// 씬 뷰의 픽셀 (0..1 정규화) 을 지나는 광선으로 가장 가까운 오브젝트를 고름. 없으면 -1. outDistance = 월드 거리.
	int Pick(Scene& scene, const Camera& camera, float u, float v, float* outDistance = nullptr) const;

	// 11-B단계
	void DrawContentBrowser(Engine& engine, const Callbacks& callbacks);
	void DrawDropGhost(Engine& engine, float x, float y, float width, float height, const AssetDragPayload& asset, const DirectX::XMFLOAT3& point, int hitObject);
	void HandleAssetDrop(Engine& engine, const Callbacks& callbacks, const AssetDragPayload& asset, const DirectX::XMFLOAT3& point, int hitObject);
	void PlaceModelFromAsset(Engine& engine, const Callbacks& callbacks, const std::wstring& relativePath, const DirectX::XMFLOAT3& position);
	void LoadSceneFromAsset(const Callbacks& callbacks, const std::wstring& relativePath);
	// 11-C단계
	void DrawPlayControls(const Callbacks& callbacks);
	void DrawComponents(GameObject& object);
	void DrawCameraGizmos(Engine& engine, float x, float y, float width, float height);   // 씬 안의 카메라 오브젝트 프러스텀
	void DrawCameraPreview(Engine& engine, float x, float y, float width, float height);  // 선택한 카메라의 시점을 우측 하단에 표시
	Camera m_previewCamera;   // 선택한 CameraComponent 의 자세를 복사해 Renderer 에 넘김 (Render 까지 살아 있어야 함)
	char m_newScriptName[64] = {};   // Inspector 의 New Script 팝업
	std::string m_newScriptError;
	// 11-D단계: File > Build Game...
	void DrawBuildPopup();
	bool m_openBuildPopup = false;
	// 11-E단계: 프로젝트 런처·설정
	void DrawLauncher(Engine& engine, const Callbacks& callbacks);
	void DrawProjectSettingsPopup(const Callbacks& callbacks);
	void DrawProjectBanner(const Callbacks& callbacks);
	bool m_openLauncher = false;
	bool m_openProjectSettings = false;
	char m_newProjectName[48] = "MyGame";
	std::wstring m_newProjectParent;
	std::string m_launcherMessage;
	GameBuilder::Options m_buildOptions;
	std::vector<std::string> m_buildScenes;
	std::string m_buildMessage;

	ContentBrowser m_contentBrowser;
	bool m_assetDragActive = false;
	// 몇 프레임 동안 SetWindowFocus 를 반복함. 첫 프레임에는 뒤이어 처음 나타나는 창(Console)이 포커스를 가져가기 때문임.
	int m_focusContentBrowserFrames = 0;

	bool m_sceneHovered = false;
	bool m_sceneFocused = false;
	bool m_gizmoUsing = false;
	uint32_t m_sceneWidth = 0;
	uint32_t m_sceneHeight = 0;
	int m_selected = -1;
	int m_gizmoOperation = 0;   // 0 이동, 1 회전, 2 크기
	bool m_gizmoWorld = false;   // 기본 로컬(오브젝트 축). 툴바의 Local/World 로 고름
	bool m_layoutBuilt = false;
	char m_pathBuffer[260] = {};
	std::string m_lastMessage;
};

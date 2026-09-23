#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <DirectXMath.h>

class Engine;
class Scene;
class Camera;
struct ModelStats;

// 에디터 UI (11단계). ImGui Dockspace 위의 창들: Scene(오프스크린 씬 뷰 + ImGuizmo + 클릭 선택), Hierarchy,
// Inspector(Transform·재질 편집), Render Settings(백엔드·PSO 통계·디버그 뷰·그림자), Lights, Materials, Stats, Console.
//
// 소유하지 않는다: 씬·카메라·렌더러는 Engine 의 것이고, 씬 전환·저장·로드는 콜백으로 앱(TestApp)에 넘긴다.
// 패널은 데이터를 "편집"하는 쪽이지 "소유"하는 쪽이 아니다 (3단계 DebugUI 의 규칙 그대로).
class Editor
{
public:
	struct Callbacks
	{
		std::function<void(int)> switchScene;                  // 0 데모, 1 헬멧, 2 Sponza
		std::function<bool(const std::wstring&)> saveScene;
		std::function<bool(const std::wstring&)> loadScene;
		std::function<void()> newScene;                       // 바닥만 있는 새 씬
		std::function<void(int)> addPrimitive;                 // 0 구, 1 큐브, 2 원기둥, 3 평면 — 원점 위에 기본 재질로
		std::function<void(int)> deleteObject;                 // 오브젝트 인덱스
	};

	Editor();

	// 프레임마다 ImGui::NewFrame 뒤에. 도킹 레이아웃·메뉴·모든 창을 그린다.
	void Draw(Engine& engine, const char* sceneName, const ModelStats& modelStats, const Callbacks& callbacks);

	// ---- 씬 뷰 상태 (앱의 카메라 컨트롤러가 본다) ----
	bool IsSceneViewHovered() const { return m_sceneHovered; }
	bool IsSceneViewFocused() const { return m_sceneFocused; }
	bool IsGizmoUsing() const { return m_gizmoUsing; }
	uint32_t GetSceneViewWidth() const { return m_sceneWidth; }
	uint32_t GetSceneViewHeight() const { return m_sceneHeight; }

	int GetSelectedObject() const { return m_selected; }
	void SetSelectedObject(int index) { m_selected = index; }
	void ClearSelection() { m_selected = -1; }
	// 자동 검증용: 클릭과 같은 코드 경로로 (u, v) 픽셀의 오브젝트를 고른다.
	int PickAt(Scene& scene, const Camera& camera, float u, float v) const { return Pick(scene, camera, u, v); }

	bool showConsole = true;
	float cameraMoveSpeed = 10.0f;   // Render Settings 의 카메라 패널이 편집, 앱의 컨트롤러가 읽는다
	std::wstring scenePath;   // 저장/로드 경로 (메뉴의 텍스트 필드)

private:
	void DrawMenuBar(Engine& engine, const Callbacks& callbacks);
	void BuildDefaultLayout(uint32_t dockspaceId);
	void DrawSceneView(Engine& engine);
	void DrawHierarchy(Engine& engine, const Callbacks& callbacks);
	void DrawInspector(Engine& engine);
	void DrawRenderSettings(Engine& engine);
	void DrawStats(Engine& engine, const char* sceneName, const ModelStats& modelStats);
	void DrawGizmo(Engine& engine, float x, float y, float width, float height);
	// 씬 뷰의 픽셀 (0..1 정규화) 을 지나는 광선으로 가장 가까운 오브젝트를 고른다. 없으면 -1.
	int Pick(Scene& scene, const Camera& camera, float u, float v) const;

	bool m_sceneHovered = false;
	bool m_sceneFocused = false;
	bool m_gizmoUsing = false;
	uint32_t m_sceneWidth = 0;
	uint32_t m_sceneHeight = 0;
	int m_selected = -1;
	int m_gizmoOperation = 0;   // 0 이동, 1 회전, 2 크기
	bool m_gizmoWorld = true;
	bool m_layoutBuilt = false;
	char m_pathBuffer[260] = {};
	std::string m_lastMessage;
};

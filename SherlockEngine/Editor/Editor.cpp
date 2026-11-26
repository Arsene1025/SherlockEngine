#include "pch.h"
#include "Editor/Editor.h"
#include "Editor/DebugUI.h"
#include "Editor/ScriptCreator.h"
#include "Editor/GameBuilder.h"
#include "Editor/ProjectLauncher.h"
#include "Core/Engine.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "Core/Paths.h"
#include "Core/AssetManager.h"
#include "Graphics/DebugDraw.h"
#include "Graphics/Mesh.h"
#include "Graphics/Model.h"
#include "Scene/CameraComponent.h"
#include "UI/UIComponents.h"
#include "UI/UIEvent.h"
#include "UI/UISystem.h"
#include "RHI/Device.h"
#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder (아직 내부 API)
#include <ImGuizmo.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>   // C++20: 드롭 라벨 서식
#include <string_view>
#include <unordered_map>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	// 회전 행렬 → 오일러 각 변환은 Transform::EulerFromRotationMatrix 가 맡음 (재부모화·FollowTarget 도 같은
	// 식을 쓰기 때문).

	// 광선-AABB 교차 판정 (slab). t ≥ 0 인 가장 가까운 교차점을 구함. 없으면 false.
	bool RayAabb(const XMFLOAT3& origin, const XMFLOAT3& direction, const XMFLOAT3& boxMin, const XMFLOAT3& boxMax, float& outT)
	{
		float tMin = 0.0f;
		float tMax = FLT_MAX;
		const float o[3] = { origin.x, origin.y, origin.z };
		const float d[3] = { direction.x, direction.y, direction.z };
		const float lo[3] = { boxMin.x, boxMin.y, boxMin.z };
		const float hi[3] = { boxMax.x, boxMax.y, boxMax.z };
		for (int i = 0; i < 3; ++i)
		{
			if (fabsf(d[i]) < 1e-8f)
			{
				if (o[i] < lo[i] || o[i] > hi[i]) return false;
				continue;
			}
			float t1 = (lo[i] - o[i]) / d[i];
			float t2 = (hi[i] - o[i]) / d[i];
			if (t1 > t2)
			{
				std::swap(t1, t2);
			}
			tMin = (std::max)(tMin, t1);
			tMax = (std::min)(tMax, t2);
			if (tMin > tMax) return false;
		}
		outT = tMin;
		return true;
	}

	// 씬 뷰 픽셀 (u, v ∈ 0..1) → 월드 광선. near/far 점을 역투영함 (Pick 과 드롭 위치 계산이 함께 씀).
	struct Ray
	{
		XMVECTOR origin;
		XMVECTOR direction;   // 정규화
	};
	Ray ScreenToRay(const Camera& camera, float u, float v)
	{
		const float ndcX = u * 2.0f - 1.0f;
		const float ndcY = 1.0f - v * 2.0f;
		const XMMATRIX invViewProj = XMMatrixInverse(nullptr, camera.GetViewMatrix() * camera.GetProjectionMatrix());
		const XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
		const XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);
		return Ray{ nearPoint, XMVector3Normalize(farPoint - nearPoint) };
	}

	// 월드 점 → 씬 뷰 픽셀. XMVector3Transform (Coord 가 아니라) 로 w 를 남겨 카메라 뒤의 점을 걸러 냄.
	bool ProjectToView(const XMMATRIX& viewProj, float x, float y, float width, float height, const XMFLOAT3& p, ImVec2& out)
	{
		const XMVECTOR clip = XMVector3Transform(XMLoadFloat3(&p), viewProj);
		const float w = XMVectorGetW(clip);
		if (w <= 1e-4f) return false;
		out = ImVec2(x + (XMVectorGetX(clip) / w * 0.5f + 0.5f) * width, y + (0.5f - XMVectorGetY(clip) / w * 0.5f) * height);
		return true;
	}

	// 월드 선분 → 씬 뷰 픽셀 선분. ProjectToView 와 달리 한 끝만 카메라 뒤면 버리지 않고 w = 근평면 근처에서 잘라 앞쪽 몫을 그림
	// (컬링 고정 오버레이: 고정 카메라 절두체의 먼 변은 에디터 카메라 뒤로 지나가는 일이 흔함).
	bool ProjectSegmentToView(const XMMATRIX& viewProj, float x, float y, float width, float height, const XMFLOAT3& p0, const XMFLOAT3& p1, ImVec2& out0, ImVec2& out1)
	{
		constexpr float kMinW = 1e-3f;
		XMVECTOR c0 = XMVector3Transform(XMLoadFloat3(&p0), viewProj);
		XMVECTOR c1 = XMVector3Transform(XMLoadFloat3(&p1), viewProj);
		const float w0 = XMVectorGetW(c0), w1 = XMVectorGetW(c1);
		if (w0 <= kMinW && w1 <= kMinW) return false;
		if (w0 <= kMinW) c0 = XMVectorLerp(c0, c1, (kMinW - w0) / (w1 - w0));
		else if (w1 <= kMinW) c1 = XMVectorLerp(c1, c0, (kMinW - w1) / (w0 - w1));
		const auto toPixel = [&](const XMVECTOR& c)
		{
			const float w = XMVectorGetW(c);
			return ImVec2(x + (XMVectorGetX(c) / w * 0.5f + 0.5f) * width, y + (0.5f - XMVectorGetY(c) / w * 0.5f) * height);
		};
		out0 = toPixel(c0);
		out1 = toPixel(c1);
		return true;
	}

	// 조명 아이콘·기즈모가 앉는 월드 위치. Point·Spot 은 position. Directional 은 위치가 없으므로 씬 뷰
	// 오른쪽 위 모서리에 화면 고정으로 세로로 쌓음 (카메라를 돌려도 같은 자리) — 그 화면 점을 카메라 앞 8
	// 단위로 역투영한 점을 앵커로 씀.
	constexpr float kLightPickRadius = 14.0f;
	XMFLOAT3 LightAnchor(const Camera& camera, const std::vector<Light>& lights, int index, float x, float y, float width, float height)
	{
		const Light& light = lights[index];
		if (light.type != LightType::Directional) return light.position;
		int ordinal = 0;
		for (int i = 0; i < index; ++i)
		{
			if (lights[i].type == LightType::Directional)
			{
				++ordinal;
			}
		}
		const float u = (width - 130.0f) / width;
		const float v = (110.0f + ordinal * 42.0f) / height;
		const Ray ray = ScreenToRay(camera, u, v);
		XMFLOAT3 anchor;
		XMStoreFloat3(&anchor, ray.origin + ray.direction * 8.0f);
		return anchor;
	}

	// 광원 방향(+Z)을 향하는 회전 + 이동 행렬 (DirectXMath 행벡터: 행 0 = right, 1 = up, 2 = forward, 3 = 이동)
	XMFLOAT4X4 LightGizmoMatrix(const Light& light, const XMFLOAT3& anchor, bool withRotation)
	{
		XMFLOAT4X4 m;
		XMStoreFloat4x4(&m, XMMatrixIdentity());
		if (withRotation)
		{
			XMVECTOR f = XMLoadFloat3(&light.direction);
			f = XMVectorGetX(XMVector3LengthSq(f)) > 1e-8f ? XMVector3Normalize(f) : XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
			const XMVECTOR refUp = fabsf(XMVectorGetY(f)) > 0.99f ? XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f) : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			const XMVECTOR r = XMVector3Normalize(XMVector3Cross(refUp, f));
			const XMVECTOR u = XMVector3Cross(f, r);
			XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&m.m[0][0]), r);
			XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&m.m[1][0]), u);
			XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&m.m[2][0]), f);
		}
		m.m[3][0] = anchor.x;
		m.m[3][1] = anchor.y;
		m.m[3][2] = anchor.z;
		return m;
	}

	std::string FileStem(const std::wstring& relativePath)
	{
		std::string s = Log::ToUtf8(relativePath.c_str());
		const size_t slash = s.find_last_of("\\/");
		if (slash != std::string::npos)
		{
			s = s.substr(slash + 1);
		}
		const size_t dot = s.find_last_of('.');
		if (dot != std::string::npos && dot > 0)
		{
			s = s.substr(0, dot);
		}
		return s;
	}
}

Editor::Editor()
{
}

void Editor::Draw(Engine& engine, const char* sceneName, const ModelStats& modelStats, const Callbacks& callbacks)
{
	if (scenePath.empty())
	{
		scenePath = Paths::GetExecutableDir() + L"Scenes\\scene.json";
	}

	// ---- 도킹 공간 (메인 뷰포트 전체, 메뉴 바 아래) ----
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiID dockspaceId = ImGui::GetID("SherlockDockspace");
	if (!m_layoutBuilt && ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
	{
		BuildDefaultLayout(dockspaceId);
	}
	m_layoutBuilt = true;
	ImGui::DockSpaceOverViewport(dockspaceId, viewport);

	// 콘텐츠 브라우저 에셋을 끌고 있는지 확인. IsDragDropActive 는 internal 이라 페이로드로 판단함.
	const ImGuiPayload* dragPayload = ImGui::GetDragDropPayload();
	m_assetDragActive = dragPayload != nullptr && dragPayload->IsDataType(kAssetPayloadType);

	SyncSelection(engine.GetScene());   // 오브젝트 벡터가 바뀌었을 수 있으니(명령 큐·재생 중 Destroy·되돌리기) 먼저 ID 로 인덱스를 다시 잡음
	DrawMenuBar(engine, callbacks);

	// 단축키: Ctrl+S 저장, Ctrl+L 로드, 1/2/3 기즈모 (텍스트 입력 중이 아닐 때)
	if (!ImGui::GetIO().WantTextInput)
	{
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && callbacks.saveScene)
		{
			m_lastMessage = callbacks.saveScene(scenePath) ? "saved" : "save failed";
		}
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L) && callbacks.loadScene)
		{
			callbacks.loadScene(scenePath);   // 결과 메시지·선택 비우기는 명령이 함
		}
		// 되돌리기·다시 실행·복제. 위젯을 잡고 있는 동안(슬라이더 드래그 등)은 무시함 — 편집이 아직 기록되기 전이므로.
		if (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z) && callbacks.undo)
			{
				callbacks.undo();
			}
			if ((ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) && callbacks.redo)
			{
				callbacks.redo();
			}
			if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D) && callbacks.duplicateObject && m_selected >= 0)
			{
				callbacks.duplicateObject(m_selected);
			}
		}
		if (ImGui::IsKeyPressed(ImGuiKey_1, false))
		{
			m_gizmoOperation = 0;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_2, false))
		{
			m_gizmoOperation = 1;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_3, false))
		{
			m_gizmoOperation = 2;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_T, false) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
		{
			m_gizmoOperation = 3;   // Rect 툴 (유니티 T)
		}
		// Ctrl+P 재생/정지 (언리얼 Alt+P, 유니티 Ctrl+P)
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P))
		{
			if (playState == PlayState::Editing)
			{
				if (callbacks.play)
				{
					callbacks.play();
				}
			}
			else if (callbacks.stop)
			{
				callbacks.stop();
			}
		}
	}
	DrawSceneView(engine, callbacks);
	DrawHierarchy(engine, callbacks);
	DrawInspector(engine);
	DrawRenderSettings(engine);
	DrawStats(engine, sceneName, modelStats);
	DrawContentBrowser(engine, callbacks);   // Stats 다음에 그림 — Stats 창의 도킹 노드에 따라 붙음

	DebugUI::BeginPanel("Lights");
	DebugUI::DrawEnvironmentPanel(engine.GetScene());   // 스카이박스·IBL — 주변광과 같은 "환경" 값이라 조명 창에 둠
	DebugUI::DrawLightPanel(engine.GetScene(), &m_selectedLight, &m_openSelectedLight);
	DebugUI::EndPanel();
	DebugUI::DrawPostStagesWindow(engine.GetRenderer(), engine.GetDevice());   // 후처리 단계별 썸네일 (체크로 켬)

	DebugUI::BeginPanel("Materials");
	DebugUI::DrawMaterialPanel(engine.GetScene(), engine.GetRenderer());
	DebugUI::EndPanel();

	if (showConsole)
	{
		DebugUI::DrawLogConsole(&showConsole);
	}
}

void Editor::BuildDefaultLayout(uint32_t dockspaceId)
{
	// 첫 실행(imgui.ini 없음)의 기본 배치: 왼쪽 Hierarchy, 가운데 Scene, 오른쪽 Inspector/Render
	// Settings/Lights/Materials, 아래 Console/Stats.
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

	ImGuiID center = dockspaceId;
	ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
	ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
	ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);

	ImGui::DockBuilderDockWindow("Hierarchy", left);
	ImGui::DockBuilderDockWindow("Scene", center);
	ImGui::DockBuilderDockWindow("Inspector", right);
	ImGui::DockBuilderDockWindow("Render Settings", right);
	ImGui::DockBuilderDockWindow("Lights", right);
	ImGui::DockBuilderDockWindow("Materials", right);
	ImGui::DockBuilderDockWindow("Console (F12)", bottom);
	ImGui::DockBuilderDockWindow("Stats", bottom);
	ImGui::DockBuilderDockWindow("Content Browser", bottom);   // 같은 노드의 탭. 첫 프레임에 활성 탭으로 만듦
	ImGui::DockBuilderFinish(dockspaceId);
	m_focusContentBrowserFrames = 3;
}

void Editor::DrawMenuBar(Engine& engine, const Callbacks& callbacks)
{
	if (!ImGui::BeginMainMenuBar()) return;
	if (ImGui::BeginMenu("File"))
	{
		// 프로젝트
		if (ImGui::MenuItem("New Project..."))
		{
			m_openLauncher = true;
		}
		if (ImGui::MenuItem("Open Project...") && callbacks.openProject)
		{
			const std::wstring path = ProjectLauncher::BrowseForProjectFile(nullptr);
			if (!path.empty())
			{
				m_lastMessage = callbacks.openProject(path) ? "project opened" : "open project failed";
			}
		}
		if (ImGui::BeginMenu("Recent Projects"))
		{
			const std::vector<ProjectLauncher::Recent> recent = ProjectLauncher::LoadRecent();
			if (recent.empty())
			{
				ImGui::TextDisabled("(none)");
			}
			for (const ProjectLauncher::Recent& r : recent)
			{
				const std::string label = r.name + "   " + Log::ToUtf8(r.path.c_str());
				if (ImGui::MenuItem(label.c_str()) && callbacks.openProject)
				{
					m_lastMessage = callbacks.openProject(r.path) ? "project opened" : "open project failed";
				}
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Project Settings...", nullptr, false, project.loaded))
		{
			m_openProjectSettings = true;
		}
		if (ImGui::MenuItem("Projects (launcher)..."))
		{
			m_openLauncher = true;
		}
		ImGui::Separator();
		if (m_pathBuffer[0] == '\0')
		{
			const std::string utf8 = Log::ToUtf8(scenePath.c_str());
			strncpy_s(m_pathBuffer, utf8.c_str(), sizeof(m_pathBuffer) - 1);
		}
		ImGui::SetNextItemWidth(420.0f);
		if (ImGui::InputText("path", m_pathBuffer, sizeof(m_pathBuffer)))
		{
			const int length = MultiByteToWideChar(CP_UTF8, 0, m_pathBuffer, -1, nullptr, 0);
			scenePath.assign(length > 0 ? length - 1 : 0, L'\0');
			if (length > 1)
			{
				MultiByteToWideChar(CP_UTF8, 0, m_pathBuffer, -1, &scenePath[0], length);
			}
		}
		if (ImGui::MenuItem("Save scene", "Ctrl+S") && callbacks.saveScene)
		{
			m_lastMessage = callbacks.saveScene(scenePath) ? "saved" : "save failed";
		}
		if (ImGui::MenuItem("Load scene", "Ctrl+L") && callbacks.loadScene)
		{
			callbacks.loadScene(scenePath);   // 명령 큐. 결과 메시지는 명령이 정함
		}
		ImGui::Separator();
		if (ImGui::MenuItem("New scene (floor only)") && callbacks.newScene)
		{
			callbacks.newScene();
			m_selected = -1;
			m_lastMessage = "new scene";
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Demo scene") && callbacks.switchScene)
		{
			callbacks.switchScene(0);
			m_selected = -1;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Build Game..."))
		{
			m_openBuildPopup = true;
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit"))
	{
		const bool editing = playState == PlayState::Editing;
		const std::string undoText = history.canUndo ? "Undo " + history.undoLabel : std::string("Undo");
		const std::string redoText = history.canRedo ? "Redo " + history.redoLabel : std::string("Redo");
		if (ImGui::MenuItem(undoText.c_str(), "Ctrl+Z", false, editing && history.canUndo) && callbacks.undo)
		{
			callbacks.undo();
		}
		if (ImGui::MenuItem(redoText.c_str(), "Ctrl+Y", false, editing && history.canRedo) && callbacks.redo)
		{
			callbacks.redo();
		}
		ImGui::Separator();
		const bool hasSelection = m_selected >= 0 && m_selected < static_cast<int>(engine.GetScene().GetObjects().size());
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection) && callbacks.duplicateObject)
		{
			callbacks.duplicateObject(m_selected);
		}
		if (ImGui::MenuItem("Delete", "Del", false, hasSelection) && callbacks.deleteObject)
		{
			callbacks.deleteObject(m_selected);
			m_selected = -1;
		}
		if (ImGui::MenuItem("Save selection as Prefab", nullptr, false, hasSelection) && callbacks.savePrefab)
		{
			callbacks.savePrefab(m_selected);
		}
		ImGui::Separator();
		ImGui::TextDisabled("history: %zu steps, %.1f KB%s", history.steps, history.bytes / 1024.0, editing ? "" : " (paused during Play)");
		ImGui::EndMenu();
	}
	if (m_openBuildPopup)
	{
		ImGui::OpenPopup("Build Game");
		m_openBuildPopup = false;
	}
	DrawBuildPopup();
	if (m_openProjectSettings)
	{
		ImGui::OpenPopup("Project Settings");
		m_openProjectSettings = false;
	}
	DrawProjectSettingsPopup(callbacks);
	if (m_openLauncher)
	{
		ImGui::OpenPopup("Projects");
		m_openLauncher = false;
	}
	DrawLauncher(engine, callbacks);
	if (ImGui::BeginMenu("View"))
	{
		ImGui::MenuItem("Console (F12)", nullptr, &showConsole);
		ImGui::MenuItem("Content Browser", nullptr, &showContentBrowser);
		if (ImGui::MenuItem("Reset layout"))
		{
			m_layoutBuilt = false;
			ImGui::DockBuilderRemoveNode(ImGui::GetID("SherlockDockspace"));
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Gizmo"))
	{
		ImGui::RadioButton("Translate (1)", &m_gizmoOperation, 0);
		ImGui::RadioButton("Rotate (2)", &m_gizmoOperation, 1);
		ImGui::RadioButton("Scale (3)", &m_gizmoOperation, 2);
		if (ImGui::RadioButton("Local space", !m_gizmoWorld))
		{
			m_gizmoWorld = false;
		}
		if (ImGui::RadioButton("World space", m_gizmoWorld))
		{
			m_gizmoWorld = true;
		}
		ImGui::Separator();
		ImGui::TextDisabled("Light icons");
		if (ImGui::RadioButton("All lights", m_lightIconMode == LightIconMode::All))
		{
			m_lightIconMode = LightIconMode::All;
		}
		if (ImGui::RadioButton("Selected light only", m_lightIconMode == LightIconMode::SelectedOnly))
		{
			m_lightIconMode = LightIconMode::SelectedOnly;
		}
		ImGui::EndMenu();
	}
	ImGui::Text("   %s   |   %s   |   backend %s   |   %s", m_lastMessage.c_str(), project.loaded ? project.name.c_str() : "(no project)", engine.GetDevice().GetBackendName(), m_selected >= 0 && m_selected < static_cast<int>(engine.GetScene().GetObjects().size()) ? engine.GetScene().GetObjects()[m_selected]->GetName().c_str() : "(no selection)");
	ImGui::EndMainMenuBar();
}

void Editor::DrawSceneView(Engine& engine, const Callbacks& callbacks)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Scene");
	ImGui::PopStyleVar();

	// 툴바 한 줄. 재생 버튼이 맨 앞.
	ImGui::SetCursorPos(ImVec2(6.0f, 4.0f + ImGui::GetCursorPosY()));
	DrawPlayControls(callbacks);
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	ImGui::RadioButton("Move", &m_gizmoOperation, 0);
	ImGui::SameLine();
	ImGui::RadioButton("Rotate", &m_gizmoOperation, 1);
	ImGui::SameLine();
	ImGui::RadioButton("Scale", &m_gizmoOperation, 2);
	ImGui::SameLine();
	ImGui::RadioButton("Rect", &m_gizmoOperation, 3);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("T: UI rect tool — drag inside to move, handles to resize (Alt: from center, Shift: keep aspect), pivot circle, anchor triangles. Snap 8 px (Ctrl: off). Arrows: 1 unit (Shift 10)");
	}
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	if (ImGui::RadioButton("Local", !m_gizmoWorld))
	{
		m_gizmoWorld = false;
	}
	ImGui::SameLine();   // 기본: 오브젝트 축 기준
	if (ImGui::RadioButton("World", m_gizmoWorld))
	{
		m_gizmoWorld = true;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("|");
	ImGui::SameLine();
	ImGui::Checkbox("UI", &m_showUI);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("draw screen-space canvases over the Scene view while editing (always drawn while playing)");
	}
	engine.SetUIVisible(m_showUI || playState != PlayState::Editing);   // 이번 프레임 Engine::Render 의 UI Build 가 읽음
	ImGui::SameLine();
	ImGui::TextDisabled("RMB: look  WASD: move  LMB: select  1/2/3/T: gizmo  Ctrl+P: play");

	// 스크립트 DLL 이 안 올라왔거나 새 빌드가 있으면 씬 뷰 위(이미지 앞)에 배너를 표시 — 이미지가 남은 공간을
	// 다 쓰므로 이미지보다 먼저 그림
	DrawProjectBanner(callbacks);

	const ImVec2 available = ImGui::GetContentRegionAvail();
	const uint32_t width = static_cast<uint32_t>((std::max)(available.x, 8.0f));
	const uint32_t height = static_cast<uint32_t>((std::max)(available.y, 8.0f));
	m_sceneWidth = width;
	m_sceneHeight = height;
	Renderer& renderer = engine.GetRenderer();
	renderer.SetSceneTarget(width, height);   // 크기가 같으면 아무것도 하지 않음
	if (width > 0 && height > 0)
	{
		engine.GetCamera().SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
	}

	const ImVec2 imagePos = ImGui::GetCursorScreenPos();
	m_sceneImageX = imagePos.x;
	m_sceneImageY = imagePos.y;
	const uint64_t textureId = renderer.IsOffscreen() ? engine.GetDevice().GetImGuiTextureId(renderer.GetSceneTexture()) : 0;
	if (textureId != 0)
	{
		ImGui::Image(static_cast<ImTextureID>(textureId), ImVec2(static_cast<float>(width), static_cast<float>(height)));
	}
	else
	{
		ImGui::Dummy(ImVec2(static_cast<float>(width), static_cast<float>(height)));
	}
	m_sceneHovered = ImGui::IsItemHovered();   // 드래그 중에는 ImGui 가 false 를 돌려줌 (활성 소스 아이템이 막음) — 드롭 판정은 아래 드롭 타깃에서만 함
	m_sceneFocused = ImGui::IsWindowFocused();
	// 게임 UI 포인터 = 씬 이미지 사각형 (다음 프레임 UISystem::Update 가 씀). 다른 창이 위를 덮으면 꺼서 클릭이 새지 않게 —
	// 누른 채 밖으로 끄는 드래그(슬라이더)는 계속. 자동화 입력은 창 위 커서와 상관없이.
	UISystem& ui = engine.GetUI();
	ui.SetPointerViewport(imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height), m_sceneHovered || ui.IsPointerCaptured() || engine.GetInput().IsSimulated());
	const bool editing = playState == PlayState::Editing;
	if (editing)
	{
		DrawWorldCanvasOutlines(engine);
	}

	// 재생 중 표시 — 언리얼 PIE 처럼 씬 뷰에 테두리를 그림 (재생 초록, 일시정지 노랑)
	if (playState != PlayState::Editing)
	{
		const ImU32 color = playState == PlayState::Playing ? IM_COL32(70, 200, 90, 255) : IM_COL32(240, 190, 40, 255);
		ImGui::GetWindowDrawList()->AddRect(imagePos, ImVec2(imagePos.x + width, imagePos.y + height), color, 0.0f, 0, 3.0f);
		const char* text = playState == PlayState::Playing ? "PLAYING" : "PAUSED";
		ImGui::GetWindowDrawList()->AddText(ImVec2(imagePos.x + 10.0f, imagePos.y + 8.0f), color, text);
	}

	// 콘텐츠 브라우저 드롭 타깃. 이미지 아이템 바로 뒤에 있어야 함.
	// AcceptBeforeDelivery: 호버 중 매 프레임 페이로드를 받아 고스트 마커를 그리고, IsDelivery() 인
	// 프레임(릴리스)에만 실제로 놓음.
	if (ImGui::BeginDragDropTarget())
	{
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayloadType, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
		if (payload != nullptr && payload->DataSize == sizeof(AssetDragPayload) && !m_contentBrowser.IsDragCancelled())
		{
			AssetDragPayload asset = *static_cast<const AssetDragPayload*>(payload->Data);
			asset.relativePath[259] = L'\0';   // 종료 문자 방어
			const ImVec2 mouse = ImGui::GetMousePos();
			const float u = (mouse.x - imagePos.x) / static_cast<float>(width);
			const float v = (mouse.y - imagePos.y) / static_cast<float>(height);
			XMFLOAT3 point;
			const int hitObject = RaycastScene(engine.GetScene(), engine.GetCamera(), u, v, point);
			DrawDropGhost(engine, imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height), asset, point, hitObject);
			if (payload->IsDelivery())
			{
				HandleAssetDrop(engine, callbacks, asset, point, hitObject);
			}
		}
		ImGui::EndDragDropTarget();
	}

	// 씬 안의 카메라 오브젝트를 프러스텀 아이콘으로 표시 (편집 중에만 — 재생 중에는 그 카메라로 보고 있음)
	DrawCameraGizmos(engine, imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height));
	DrawUISelection(engine, imagePos.x, imagePos.y);
	const bool rectToolTookPress = DrawRectTool(engine, imagePos.x, imagePos.y, editing);
	if (editing && m_sceneFocused && !ImGui::GetIO().WantTextInput && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		NudgeSelectedUI(engine);
	}
	// 선택한 카메라의 시점 미리보기 (우측 하단)
	DrawCameraPreview(engine, imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height));

	// 선택된 오브젝트의 기즈모. 이미지 위에 그림. 에셋을 끌고 있는 동안은 끔.
	DrawFreezeOverlay(engine, imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height));
	DrawLightIcons(engine, imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height));
	ImGuizmo::Enable(!m_assetDragActive);
	DrawGizmo(engine, imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height));
	DrawLightGizmo(engine, imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height));   // 조명을 골랐을 때만 (오브젝트 선택과 배타적)
	ImGuizmo::Enable(true);

	// 클릭 선택. 기즈모를 잡고 있거나 커서가 기즈모 위에 있으면 제외. 조명 아이콘이 오브젝트보다 우선함
	// (작아서 오브젝트에 가려 안 눌리는 걸 막으려고).
	// 재생 중 게임 UI 를 누른 클릭은 게임 것 (에디터 선택이 바뀌지 않게). 편집 중 UI 가 보이면 UI 사각형을 3D 보다 먼저 (7단계).
	const ToolMouse tool = GetToolMouse();
	const bool toolInside = tool.x >= 0.0f && tool.y >= 0.0f && tool.x < static_cast<float>(width) && tool.y < static_cast<float>(height);
	const bool clicked = m_toolPointer.active ? (tool.pressed && toolInside) : (m_sceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left));
	const bool uiTookClick = !editing && (ui.IsPointerOverUI() || ui.IsPointerCaptured());
	if (clicked && !rectToolTookPress && !uiTookClick && !m_assetDragActive && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
	{
		const ImVec2 mouse(imagePos.x + tool.x, imagePos.y + tool.y);
		const float u = tool.x / static_cast<float>(width);
		const float v = tool.y / static_cast<float>(height);
		const int lightHit = PickLight(engine, mouse.x, mouse.y, imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height));
		if (lightHit >= 0)
		{
			SelectLight(lightHit);
		}
		else if (!(editing && engine.IsUIVisible() && PickUI(engine, tool.x, tool.y)))
		{
			m_selectedLight = -1;
			m_selected = Pick(engine.GetScene(), engine.GetCamera(), u, v);
		}
	}
	m_toolPointer.prevDown = m_toolPointer.down;
	m_prevToolDown = tool.down;
	ImGui::End();
}

void Editor::DrawGizmo(Engine& engine, float x, float y, float width, float height)
{
	m_gizmoUsing = false;
	Scene& scene = engine.GetScene();
	if (m_selected < 0 || m_selected >= static_cast<int>(scene.GetObjects().size()) || width <= 0.0f || height <= 0.0f) return;

	GameObject& object = *scene.GetObjects()[m_selected];
	const Camera& camera = engine.GetCamera();
	// 캔버스 아래 오브젝트는 3D 기즈모가 의미 없음 (위치는 RectTransform, Rect 툴). World 캔버스 루트만 3D 로 옮김
	const GameObject* canvasObject = nullptr;
	if (const Canvas* canvas = UISystem::FindCanvas(object, &canvasObject); canvas != nullptr && !(canvas->renderMode == Canvas::World && canvasObject == &object)) return;

	// DirectXMath 행벡터 행렬과 ImGuizmo(열벡터, 열우선 메모리)는 메모리 배치가 같음 — 이동 성분이 [12..14]
	// 에 있는 것이 그 증거임.
	XMFLOAT4X4 view, proj, world;
	XMStoreFloat4x4(&view, camera.GetViewMatrix());
	XMStoreFloat4x4(&proj, camera.GetProjectionMatrix());
	XMStoreFloat4x4(&world, object.GetWorldMatrix());   // 부모까지 곱한 월드 행렬. 기즈모는 언제나 월드 공간에서 조작함

	ImGuizmo::SetOrthographic(!camera.IsUsePerspectiveProjection());   // 직교 카메라에서도 기즈모가 맞게
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(x, y, width, height);
	const ImGuizmo::OPERATION operation = m_gizmoOperation == 1 ? ImGuizmo::ROTATE : (m_gizmoOperation == 2 ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE);   // Rect(3) 는 UI 가 아니면 이동
	const ImGuizmo::MODE mode = (m_gizmoWorld && m_gizmoOperation != 2) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
	if (ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], operation, mode, &world.m[0][0]))
	{
		// world = pre × S·R·T × parent.world 이므로 GameObject 가 parent.world⁻¹ 을, Transform 이 pre⁻¹ 과
		// S·R·T 분해를 맡음. 분해가 실패하면(스케일 0) 이번 프레임 조작은 버려짐.
		object.SetWorldMatrix(XMLoadFloat4x4(&world));
	}
	m_gizmoUsing = ImGuizmo::IsUsing();
}

int Editor::Pick(Scene& scene, const Camera& camera, float u, float v, float* outDistance) const
{
	if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return -1;
	// 픽셀 → NDC → 월드 광선 (near 와 far 점을 역투영).
	const Ray ray = ScreenToRay(camera, u, v);
	const XMVECTOR nearPoint = ray.origin;
	const XMVECTOR worldDirection = ray.direction;

	int best = -1;
	float bestT = FLT_MAX;
	const auto& objects = scene.GetObjects();
	for (int i = 0; i < static_cast<int>(objects.size()); ++i)
	{
		const Mesh* mesh = objects[i]->GetMesh();
		XMFLOAT3 boxMin, boxMax;
		if (mesh != nullptr && mesh->IsValid())
		{
			boxMin = mesh->GetBounds().min;
			boxMax = mesh->GetBounds().max;
		}
		else if (objects[i]->GetBehaviour<CameraComponent>() != nullptr)
		{
			// 메시 없는 카메라 오브젝트는 1 단위 상자로 판정함 (씬 뷰의 카메라 아이콘과 같은 자리)
			boxMin = XMFLOAT3(-0.5f, -0.5f, -0.5f);
			boxMax = XMFLOAT3(0.5f, 0.5f, 0.5f);
		}
		else
		{
			continue;
		}
		// 광선을 오브젝트 로컬 공간으로 옮겨 로컬 AABB 와 검사함 (회전·스케일이 있어도 정확).
		const XMMATRIX world = objects[i]->GetWorldMatrix();   // 계층 포함
		const XMMATRIX invWorld = XMMatrixInverse(nullptr, world);
		XMFLOAT3 origin, direction;
		XMStoreFloat3(&origin, XMVector3TransformCoord(nearPoint, invWorld));
		XMStoreFloat3(&direction, XMVector3TransformNormal(worldDirection, invWorld));
		const float length = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
		if (length < 1e-8f) continue;
		float tLocal = 0.0f;
		if (!RayAabb(origin, direction, boxMin, boxMax, tLocal)) continue;
		// 로컬 t 는 스케일된 방향 기준이라 월드 거리로 바꿔 비교함.
		const float tWorld = tLocal * length;
		if (tWorld < bestT)
		{
			bestT = tWorld;
			best = i;
		}
	}
	if (outDistance != nullptr && best >= 0)
	{
		*outDistance = bestT;
	}
	return best;
}

int Editor::RaycastScene(Scene& scene, const Camera& camera, float u, float v, XMFLOAT3& outPoint) const
{
	const Ray ray = ScreenToRay(camera, u, v);
	float distance = 0.0f;
	const int hit = Pick(scene, camera, u, v, &distance);
	if (hit >= 0)
	{
		XMStoreFloat3(&outPoint, ray.origin + ray.direction * distance);
		return hit;
	}
	// y = 0 평면: t = −o.y / d.y (앞쪽일 때만)
	const float oy = XMVectorGetY(ray.origin);
	const float dy = XMVectorGetY(ray.direction);
	if (fabsf(dy) > 1e-5f && -oy / dy >= 0.0f)
	{
		XMStoreFloat3(&outPoint, ray.origin + ray.direction * (-oy / dy));
		return -1;
	}
	XMStoreFloat3(&outPoint, ray.origin + ray.direction * 10.0f);
	return -1;
}

// ------------------------------------------------------------------ 콘텐츠 브라우저 · 드롭

void Editor::DrawContentBrowser(Engine& engine, const Callbacks& callbacks)
{
	if (!showContentBrowser) return;
	// 저장된 imgui.ini 에 이 창이 없으면(예전 레이아웃) Stats 가 있는 노드의 탭으로 처음 한 번 붙임.
	// FirstUseEver 이므로 ini 에 항목이 생긴 뒤에는 사용자가 옮긴 자리를 존중함. 도킹 ID 는 바꾸지 않음.
	if (ImGuiWindow* stats = ImGui::FindWindowByName("Stats"))
	{
		if (stats->DockId != 0)
		{
			ImGui::SetNextWindowDockID(stats->DockId, ImGuiCond_FirstUseEver);
		}
	}
	ImGui::Begin("Content Browser", &showContentBrowser);
	if (m_focusContentBrowserFrames > 0)
	{
		ImGui::SetWindowFocus();
		--m_focusContentBrowserFrames;
	}
	ContentBrowser::Actions actions;
	actions.openScene = [&](const std::wstring& relativePath)
	{
		LoadSceneFromAsset(callbacks, relativePath);
	};
	actions.placeModel = [&](const std::wstring& relativePath, const XMFLOAT3& position)
	{
		PlaceModelFromAsset(engine, callbacks, relativePath, position);
	};
	actions.instantiatePrefab = [&](const std::wstring& relativePath, const XMFLOAT3& position)
	{
		if (callbacks.instantiatePrefab)
		{
			callbacks.instantiatePrefab(relativePath, position);
		}
	};
	m_contentBrowser.Draw(engine.GetDevice(), actions);
	ImGui::End();
}

void Editor::PlaceModelFromAsset(Engine& engine, const Callbacks& callbacks, const std::wstring& relativePath, const XMFLOAT3& position)
{
	(void)engine;
	if (!callbacks.placeModel) return;
	callbacks.placeModel(relativePath, position);   // 명령 큐에 들어감. 배치·선택·메시지는 앱의 명령이 실행될 때 정함
}

void Editor::LoadSceneFromAsset(const Callbacks& callbacks, const std::wstring& relativePath)
{
	if (!callbacks.loadScene) return;
	scenePath = Paths::GetAssetRoot() + relativePath;
	m_pathBuffer[0] = '\0';           // File 메뉴의 경로 필드를 다음에 열 때 다시 채움
	callbacks.loadScene(scenePath);   // 명령 큐. 결과 메시지·선택 비우기는 명령이 함
}

void Editor::HandleAssetDrop(Engine& engine, const Callbacks& callbacks, const AssetDragPayload& asset, const XMFLOAT3& point, int hitObject)
{
	const std::wstring relativePath(asset.relativePath);
	switch (asset.type)
	{
	case AssetType::Model:
		PlaceModelFromAsset(engine, callbacks, relativePath, point);
		break;
	case AssetType::Scene:
		LoadSceneFromAsset(callbacks, relativePath);
		break;
	case AssetType::Prefab:
		if (callbacks.instantiatePrefab)
		{
			callbacks.instantiatePrefab(relativePath, point);   // 명령 큐 → Scene::Instantiate
		}
		break;
	case AssetType::Texture:
	{
		// 광선에 맞은 오브젝트의 재질 알베도로 지정. 재질은 공유될 수 있어 같은 재질을 쓰는 오브젝트가 모두
		// 바뀜 (메시지로 알림).
		Scene& scene = engine.GetScene();
		if (hitObject < 0 || hitObject >= static_cast<int>(scene.GetObjects().size()))
		{
			m_lastMessage = "drop the texture onto an object";
			break;
		}
		GameObject& object = *scene.GetObjects()[hitObject];
		if (!object.GetSlotMaterials().empty())
		{
			m_lastMessage = "model objects use submesh slot materials (edit in Materials)";
			break;
		}
		Material* material = nullptr;
		for (auto& m : scene.GetMaterials())
		{
			if (m.get() == object.GetMaterial())
			{
				material = m.get();
			}
		}
		if (material == nullptr)
		{
			m_lastMessage = "object has no editable material";
			break;
		}
		material->albedoTexture = ContentBrowser::ToMaterialTextureName(relativePath);
		m_lastMessage = "albedo of '" + material->name + "' = " + material->albedoTexture;
		Log::Info("텍스처 드롭: '%s' 재질 '%s' 알베도 → %s", object.GetName().c_str(), material->name.c_str(), material->albedoTexture.c_str());
		break;
	}
	default:
		m_lastMessage = "unsupported asset type";
		break;
	}
}

void Editor::DrawDropGhost(Engine& engine, float x, float y, float width, float height, const AssetDragPayload& asset, const XMFLOAT3& point, int hitObject)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImU32 accent = IM_COL32(255, 200, 40, 255);
	const ImU32 accentSoft = IM_COL32(255, 200, 40, 110);
	dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), accent, 0.0f, 0, 2.0f);

	const Camera& camera = engine.GetCamera();
	const XMMATRIX viewProj = camera.GetViewMatrix() * camera.GetProjectionMatrix();
	const auto project = [&](const XMFLOAT3& p, ImVec2& out) -> bool
	{
		return ProjectToView(viewProj, x, y, width, height, p, out);
	};
	const auto line = [&](const XMFLOAT3& a, const XMFLOAT3& b, ImU32 color, float thickness)
	{
		ImVec2 pa, pb;
		if (project(a, pa) && project(b, pb))
		{
			dl->AddLine(pa, pb, color, thickness);
		}
	};

	// 놓일 점: 십자 + 원
	const float arm = 0.5f;
	line(XMFLOAT3(point.x - arm, point.y, point.z), XMFLOAT3(point.x + arm, point.y, point.z), accent, 2.0f);
	line(XMFLOAT3(point.x, point.y, point.z - arm), XMFLOAT3(point.x, point.y, point.z + arm), accent, 2.0f);
	ImVec2 center;
	const bool visible = project(point, center);
	if (visible)
	{
		dl->AddCircleFilled(center, 5.0f, accent);
		dl->AddCircle(center, 10.0f, accent, 0, 2.0f);
	}

	// 모델이면 놓일 자리에 AABB 를 그림 (이미 파싱된 모델만 — 호버 중에 Sponza 를 파싱하면 안 됨).
	if (asset.type == AssetType::Model)
	{
		AssetManager& assets = engine.GetAssets();
		if (assets.IsModelCached(asset.relativePath))
		{
			if (const Model* model = assets.GetModel(asset.relativePath))
			{
				const Bounds& b = model->bounds;
				const XMFLOAT3 offset(point.x, point.y - b.min.y, point.z);
				XMFLOAT3 c[8];
				for (int i = 0; i < 8; ++i)
				{
					c[i] = XMFLOAT3((i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z);
					c[i].x += offset.x;
					c[i].y += offset.y;
					c[i].z += offset.z;
				}
				static const int edges[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
				for (const auto& e : edges)
				{
					line(c[e[0]], c[e[1]], accentSoft, 1.5f);
				}
			}
		}
	}

	// 라벨: 이름 → 위치 (맞은 오브젝트 이름). C++20 std::format — 고정 320바이트 버퍼와 잘림 걱정이 없음.
	const std::string name = FileStem(asset.relativePath);
	const Scene& scene = engine.GetScene();
	const std::string_view target = hitObject >= 0 && hitObject < static_cast<int>(scene.GetObjects().size()) ? std::string_view(scene.GetObjects()[hitObject]->GetName()) : "ground";
	const std::string label = std::format("{}  ->  ({:.1f}, {:.1f}, {:.1f})  on {}", name, point.x, point.y, point.z, target);
	const ImVec2 mouse = ImGui::GetMousePos();
	const ImVec2 at(mouse.x + 18.0f, mouse.y + 14.0f);
	const ImVec2 size = ImGui::CalcTextSize(label.c_str());
	dl->AddRectFilled(ImVec2(at.x - 4.0f, at.y - 2.0f), ImVec2(at.x + size.x + 4.0f, at.y + size.y + 2.0f), IM_COL32(20, 22, 40, 210), 3.0f);
	dl->AddText(at, IM_COL32(255, 255, 255, 255), label.c_str());
}

// ------------------------------------------------------------------ UI 사각형 도우미 (Rect 툴·재부모화)

namespace
{
	// 사각형(부모 공간, 회전 전) → RectTransform 값 (앵커·피벗은 주어진 것). ComputeRectWithAnchors 의 역
	void ApplyRectValues(RectTransform& rt, const UIRect& parent, const UIRect& rect, const XMFLOAT2& anchorMin, const XMFLOAT2& anchorMax, const XMFLOAT2& pivot)
	{
		const float pw = parent.Width();
		const float ph = parent.Height();
		const XMFLOAT2 aMin(parent.min.x + anchorMin.x * pw, parent.min.y + anchorMin.y * ph);
		const XMFLOAT2 aMax(parent.min.x + anchorMax.x * pw, parent.min.y + anchorMax.y * ph);
		const XMFLOAT2 size(rect.Width(), rect.Height());
		rt.anchorMin = anchorMin;
		rt.anchorMax = anchorMax;
		rt.pivot = pivot;
		rt.sizeDelta = XMFLOAT2(size.x - (aMax.x - aMin.x), size.y - (aMax.y - aMin.y));
		const XMFLOAT2 p(rect.min.x + size.x * pivot.x, rect.min.y + size.y * pivot.y);
		rt.anchoredPosition = XMFLOAT2(p.x - (aMin.x + (aMax.x - aMin.x) * pivot.x), p.y - (aMin.y + (aMax.y - aMin.y) * pivot.y));
	}

	// 오브젝트 자신의 회전 z·스케일 (UISystem::Visit 와 같은 식, 피벗 기준의 선형 부분)
	UIAffine LocalLinear(const GameObject& object)
	{
		const XMFLOAT3& r = object.GetTransform().GetRotation();
		const XMFLOAT3& s = object.GetTransform().GetScale();
		UIAffine m;
		m.a = std::cos(r.z) * s.x;
		m.b = std::sin(r.z) * s.x;
		m.c = -std::sin(r.z) * s.y;
		m.d = std::cos(r.z) * s.y;
		return m;
	}

	// 피벗을 P → P' 로 옮길 때 화면의 사각형이 그대로 있도록 더할 이동 t = (RS − I)(P' − P)
	XMFLOAT2 PivotCorrection(const UIAffine& rs, const XMFLOAT2& from, const XMFLOAT2& to)
	{
		const XMFLOAT2 d(to.x - from.x, to.y - from.y);
		return XMFLOAT2(rs.a * d.x + rs.c * d.y - d.x, rs.b * d.x + rs.d * d.y - d.y);
	}

	XMFLOAT2 LinearApply(const UIAffine& m, const XMFLOAT2& v)
	{
		return XMFLOAT2(m.a * v.x + m.c * v.y, m.b * v.x + m.d * v.y);
	}

	// 0·0.5·1 근처면 붙임
	float SnapUnit(float v, float threshold)
	{
		for (float t : { 0.0f, 0.5f, 1.0f })
		{
			if (std::fabs(v - t) < threshold) return t;
		}
		return v;
	}

	// 재부모화 전 상태 (화면 사각형 유지용)
	struct UIKeep
	{
		bool valid = false;
		XMFLOAT2 centerCanvas{};
		XMFLOAT2 size{};
		float parentScale = 1.0f;   // 옛 부모 공간 1 단위 = 캔버스 몇 단위
	};

	UIKeep CaptureUIKeep(const UISystem* ui, const GameObject& object)
	{
		UIKeep keep;
		UISystem::ElementGeometry g;
		if (ui == nullptr || object.GetBehaviour<RectTransform>() == nullptr || !ui->GetElementGeometry(&object, g)) return keep;
		const XMFLOAT2 c((g.rect.min.x + g.rect.max.x) * 0.5f, (g.rect.min.y + g.rect.max.y) * 0.5f);
		keep.centerCanvas = g.toCanvas.Apply(c);
		keep.size = XMFLOAT2(g.rect.Width(), g.rect.Height());
		keep.parentScale = std::sqrt(std::fabs(g.parentToCanvas.a * g.parentToCanvas.d - g.parentToCanvas.b * g.parentToCanvas.c));
		keep.valid = true;
		return keep;
	}

	// 새 부모의 사각형 안에서 같은 화면 사각형이 되도록 anchoredPosition·sizeDelta 를 다시 계산 (SetParent(keepWorld) 의 UI 판)
	void RestoreUIKeep(const UISystem* ui, GameObject& object, const UIKeep& keep)
	{
		RectTransform* rt = object.GetBehaviour<RectTransform>();
		const GameObject* parent = object.GetParent();
		UISystem::ElementGeometry pg;
		if (!keep.valid || rt == nullptr || parent == nullptr || ui == nullptr || !ui->GetElementGeometry(parent, pg)) return;
		UIAffine inv;
		if (!pg.toCanvas.Invert(inv)) return;
		const float newScale = std::sqrt(std::fabs(pg.toCanvas.a * pg.toCanvas.d - pg.toCanvas.b * pg.toCanvas.c));
		const float ratio = newScale > 1e-6f ? keep.parentScale / newScale : 1.0f;
		const XMFLOAT2 size(keep.size.x * ratio, keep.size.y * ratio);
		const XMFLOAT2 cq = inv.Apply(keep.centerCanvas);
		// RS(c_r − P) + P = c_q, c_r = m + s/2, P = m + s·pivot → m = c_q − s·pivot − RS(s·(0.5 − pivot))
		const UIAffine rs = LocalLinear(object);
		const XMFLOAT2 half(size.x * (0.5f - rt->pivot.x), size.y * (0.5f - rt->pivot.y));
		const XMFLOAT2 rotated = LinearApply(rs, half);
		UIRect rect;
		rect.min = XMFLOAT2(cq.x - size.x * rt->pivot.x - rotated.x, cq.y - size.y * rt->pivot.y - rotated.y);
		rect.max = XMFLOAT2(rect.min.x + size.x, rect.min.y + size.y);
		ApplyRectValues(*rt, pg.rect, rect, rt->anchorMin, rt->anchorMax, rt->pivot);
	}
}

void Editor::DrawHierarchy(Engine& engine, const Callbacks& callbacks)
{
	DebugUI::BeginPanel("Hierarchy");
	Scene& scene = engine.GetScene();
	m_hierarchyUI = &engine.GetUI();
	auto& objects = scene.GetObjects();
	ImGui::TextWrapped("Objects %zu   Lights %zu", objects.size(), scene.GetLights().size());
	// 추가/삭제. 새 오브젝트는 원점 위에 기본 재질로 생기고 바로 선택됨.
	if (ImGui::SmallButton("+ Sphere") && callbacks.addPrimitive)
	{
		callbacks.addPrimitive(0);   // 명령 큐. 새 오브젝트 선택은 명령이 함
	}
	DebugUI::SameLineIfFits(DebugUI::ButtonWidth("+ Cube"));
	if (ImGui::SmallButton("+ Cube") && callbacks.addPrimitive)
	{
		callbacks.addPrimitive(1);
	}
	DebugUI::SameLineIfFits(DebugUI::ButtonWidth("+ Cylinder"));
	if (ImGui::SmallButton("+ Cylinder") && callbacks.addPrimitive)
	{
		callbacks.addPrimitive(2);
	}
	DebugUI::SameLineIfFits(DebugUI::ButtonWidth("+ Plane"));
	if (ImGui::SmallButton("+ Plane") && callbacks.addPrimitive)
	{
		callbacks.addPrimitive(3);
	}
	DebugUI::SameLineIfFits(DebugUI::ButtonWidth("+ Camera"));
	if (ImGui::SmallButton("+ Camera") && callbacks.addPrimitive)
	{
		callbacks.addPrimitive(4);   // 현재 에디터 시점 그대로 카메라 오브젝트 추가
	}
	DebugUI::SameLineIfFits(DebugUI::ButtonWidth("+ UI"));
	if (ImGui::SmallButton("+ UI"))
	{
		ImGui::OpenPopup("AddUIPopup");
	}
	if (ImGui::BeginPopup("AddUIPopup"))
	{
		// 선택한 오브젝트가 캔버스 아래면 그 자식으로 (앱이 판단)
		if (ImGui::MenuItem("Canvas") && callbacks.addUI)
		{
			callbacks.addUI(0, m_selected);
		}
		if (ImGui::MenuItem("Image") && callbacks.addUI)
		{
			callbacks.addUI(1, m_selected);
		}
		if (ImGui::MenuItem("Text") && callbacks.addUI)
		{
			callbacks.addUI(2, m_selected);
		}
		// 위젯 묶음 (유니티 GameObject > UI 메뉴와 같은 자식 구조, 기본 둥근 사각형 builtin:ui-panel 9-slice)
		static const char* const kWidgets[] = { "Button", "Toggle", "Slider", "Scroll View", "Panel", "Event System" };
		for (int i = 0; i < 6; ++i)
		{
			if (ImGui::MenuItem(kWidgets[i]) && callbacks.addUI)
			{
				callbacks.addUI(3 + i, m_selected);
			}
		}
		ImGui::EndPopup();
	}
	const bool canDelete = m_selected >= 0 && m_selected < static_cast<int>(objects.size());
	const bool deleteKey = canDelete && (m_sceneFocused || ImGui::IsWindowFocused()) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false);
	if (canDelete)
	{
		DebugUI::SameLineIfFits(DebugUI::ButtonWidth("Delete (Del)"));
	}
	if (canDelete && (ImGui::SmallButton("Delete (Del)") || deleteKey) && callbacks.deleteObject)
	{
		callbacks.deleteObject(m_selected);
		m_selected = -1;
	}
	// 복제와 프리팹 저장 (Edit 메뉴와 같은 콜백)
	if (canDelete && m_selected >= 0)
	{
		DebugUI::SameLineIfFits(DebugUI::ButtonWidth("Duplicate"));
		if (ImGui::SmallButton("Duplicate") && callbacks.duplicateObject)
		{
			callbacks.duplicateObject(m_selected);
		}
		DebugUI::SameLineIfFits(DebugUI::ButtonWidth("Save Prefab"));
		if (ImGui::SmallButton("Save Prefab") && callbacks.savePrefab)
		{
			callbacks.savePrefab(m_selected);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("save the selected object and its children as Assets\\Prefabs\\<name>.prefab");
		}
	}
	ImGui::Separator();
	// 평면 목록 대신 트리. 루트(부모 없음)부터 그리고 자식은 재귀. 선택은 여전히 씬 벡터의 인덱스이므로
	// 포인터 → 인덱스 표를 한 번 만든다. 드래그 앤 드롭으로 재부모화: 항목을 다른 항목 위에 놓으면 그 자식이
	// 되고(월드 자세 유지), 빈 곳에 놓으면 루트가 됨.
	std::unordered_map<const GameObject*, int> indexOf;
	indexOf.reserve(objects.size());
	for (int i = 0; i < static_cast<int>(objects.size()); ++i)
	{
		indexOf[objects[i].get()] = i;
	}
	for (int i = 0; i < static_cast<int>(objects.size()); ++i)
	{
		if (objects[i]->GetParent() == nullptr)
		{
			DrawHierarchyNode(scene, *objects[i], indexOf);
		}
	}
	// 빈 공간에 드롭 → 루트로
	ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, (std::max)(ImGui::GetContentRegionAvail().y, 24.0f)));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SHERLOCK_OBJECT"))
		{
			const int dragged = *static_cast<const int*>(payload->Data);
			if (GameObject* object = scene.GetObject(dragged < 0 ? SIZE_MAX : static_cast<size_t>(dragged)))
			{
				if (object->SetParent(nullptr))
				{
					Log::Info("계층: '%s' 를 루트로", object->GetName().c_str());
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
	{
		m_selected = -1;
	}
	DebugUI::EndPanel();
}

void Editor::DrawHierarchyNode(Scene& scene, GameObject& object, const std::unordered_map<const GameObject*, int>& indexOf)
{
	const auto found = indexOf.find(&object);
	const int index = found != indexOf.end() ? found->second : -1;
	ImGui::PushID(&object);
	std::string label = object.GetName().empty() ? "(unnamed)" : object.GetName();
	if (!object.GetBehaviours().empty())
	{
		label += "  [" + std::to_string(object.GetBehaviours().size()) + "]";   // 컴포넌트 수
	}
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	// TreePop 여부는 TreeNodeEx 를 부르는 시점의 자식 유무로 고정함. 아래 드롭 처리에서 SetParent 가 자식
	// 목록을 바꾸므로, 잎(NoTreePushOnOpen — 푸시 안 함)으로 그린 노드가 같은 프레임에 자식을 얻으면 다시
	// 물어봤을 때 짝 없는 TreePop 이 나감.
	const bool hadChildren = !object.GetChildren().empty();
	if (!hadChildren)
	{
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	if (m_selected == index)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		m_selected = index;
	}

	// 드래그 소스: 페이로드는 씬 인덱스. 드롭 대상: 이 오브젝트의 자식으로 (자기 자신·자손이면 SetParent 가 거부).
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		ImGui::SetDragDropPayload("SHERLOCK_OBJECT", &index, sizeof(index));
		ImGui::TextUnformatted(object.GetName().c_str());
		ImGui::EndDragDropSource();
	}
	if (ImGui::BeginDragDropTarget())
	{
		// 항목의 위·아래 1/4 = 그 형제 앞·뒤에 끼움 (선 표시), 가운데 = 자식으로. 루트끼리는 순서를 바꾸지 않으므로 늘 자식
		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();
		const float mouseY = ImGui::GetMousePos().y;
		const float quarter = (itemMax.y - itemMin.y) * 0.25f;
		int zone = 0;
		if (object.GetParent() != nullptr)
		{
			zone = mouseY < itemMin.y + quarter ? -1 : (mouseY > itemMax.y - quarter ? 1 : 0);
		}
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SHERLOCK_OBJECT", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
		if (payload != nullptr && payload->DataSize == sizeof(int))
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
			if (zone == 0)
			{
				dl->AddRect(itemMin, itemMax, color, 0.0f, 0, 2.0f);
			}
			else
			{
				const float lineY = zone < 0 ? itemMin.y : itemMax.y;
				dl->AddLine(ImVec2(itemMin.x, lineY), ImVec2(itemMax.x, lineY), color, 2.0f);
			}
		}
		if (payload != nullptr && payload->IsDelivery() && payload->DataSize == sizeof(int))
		{
			const int dragged = *static_cast<const int*>(payload->Data);
			GameObject* child = scene.GetObject(dragged < 0 ? SIZE_MAX : static_cast<size_t>(dragged));
			GameObject* newParent = zone == 0 ? &object : object.GetParent();
			if (child != nullptr && child != &object)
			{
				if (!ReparentKeepingUI(m_hierarchyUI, *child, newParent))   // UI 면 화면 사각형 그대로 (새 부모 기준 값으로)
				{
					Log::Warn("계층: '%s' 를 '%s' 아래로 옮길 수 없음 (자기 자신 또는 자손)", child->GetName().c_str(), newParent->GetName().c_str());
				}
				else
				{
					if (zone != 0)
					{
						size_t target = object.GetSiblingIndex() + (zone > 0 ? 1 : 0);
						if (child->GetSiblingIndex() < target)
						{
							--target;   // 자기 자리를 빼면 한 칸 당겨짐
						}
						child->SetSiblingIndex(target);
						Log::Info("계층: '%s' → '%s' %s", child->GetName().c_str(), object.GetName().c_str(), zone < 0 ? "앞" : "뒤");
					}
					else
					{
						Log::Info("계층: '%s' → '%s' 의 자식", child->GetName().c_str(), object.GetName().c_str());
					}
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	// 형제 순서 (UI 는 먼저 그린 것이 아래). 드래그 재부모화는 끝에 붙이므로 순서는 여기서 바꿈
	if (ImGui::BeginPopupContextItem("##order"))
	{
		m_selected = index;
		const size_t sibling = object.GetSiblingIndex();
		const size_t siblings = object.GetParent() != nullptr ? object.GetParent()->GetChildren().size() : 0;
		if (ImGui::MenuItem("Move Up", nullptr, false, object.GetParent() != nullptr && sibling > 0))
		{
			object.SetSiblingIndex(sibling - 1);
		}
		if (ImGui::MenuItem("Move Down", nullptr, false, object.GetParent() != nullptr && sibling + 1 < siblings))
		{
			object.SetSiblingIndex(sibling + 1);
		}
		if (ImGui::MenuItem("Move to First", nullptr, false, object.GetParent() != nullptr && sibling > 0))
		{
			object.SetSiblingIndex(0);
		}
		if (ImGui::MenuItem("Move to Last", nullptr, false, object.GetParent() != nullptr && sibling + 1 < siblings))
		{
			object.SetSiblingIndex(siblings - 1);
		}
		ImGui::EndPopup();
	}

	if (open && hadChildren)
	{
		// 자식 목록을 복사해 순회함: 드롭으로 재부모화하면 순회 중인 벡터가 바뀔 수 있음.
		const std::vector<GameObject*> children = object.GetChildren();
		// 자식이 전부 잎이면 모든 행의 높이가 같으므로 화면에 보이는 행만 그림 (ImGuiListClipper). 수천 개를 한
		// 부모 밑에 두는 씬(맵 배치)에서 매 프레임 전부 제출하면 GUI 가 수십 ms 를 먹음. 펼친 자식이 섞이면 행
		// 높이가 달라 클리퍼를 쓸 수 없으므로 예전처럼 전부 그림.
		const bool allLeaves = children.size() > 64 && std::all_of(children.begin(), children.end(), [](const GameObject* c)
		{
			return c->GetChildren().empty();
		});
		if (allLeaves)
		{
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(children.size()));
			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					DrawHierarchyNode(scene, *children[row], indexOf);
				}
			}
		}
		else
		{
			for (GameObject* child : children)
			{
				DrawHierarchyNode(scene, *child, indexOf);
			}
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void Editor::SelectLight(int index)
{
	m_selectedLight = index;
	m_selected = -1;   // 오브젝트 선택은 비움 (ID 는 다음 프레임 SyncSelection 이 정리)
	m_selectedPrev = -2;
	m_openSelectedLight = true;   // Lights 패널의 해당 노드를 펼침
}

int Editor::PickLight(Engine& engine, float mouseX, float mouseY, float x, float y, float width, float height) const
{
	if (playState != PlayState::Editing || width <= 0.0f || height <= 0.0f) return -1;
	const Camera& camera = engine.GetCamera();
	const std::vector<Light>& lights = engine.GetScene().GetLights();
	const XMMATRIX viewProj = camera.GetViewMatrix() * camera.GetProjectionMatrix();
	int best = -1;
	float bestDistSq = kLightPickRadius * kLightPickRadius;
	for (int i = 0; i < static_cast<int>(lights.size()); ++i)
	{
		if (m_lightIconMode == LightIconMode::SelectedOnly && i != m_selectedLight) continue;   // 보이지 않는 아이콘은 눌리지 않음
		ImVec2 p;
		if (!ProjectToView(viewProj, x, y, width, height, LightAnchor(camera, lights, i, x, y, width, height), p)) continue;
		const float dx = p.x - mouseX, dy = p.y - mouseY;
		const float d = dx * dx + dy * dy;
		if (d < bestDistSq)
		{
			bestDistSq = d;
			best = i;
		}
	}
	return best;
}

// 컬링 고정 디버그 오버레이 (docs/plan/freeze-culling-camera-plan.html). 고정 중일 때만 그림 — 고정하지 않으면 지금 카메라
// 자신의 절두체라 변이 화면 가장자리에만 얹혀 정보가 없음. 한 끝이 에디터 카메라 뒤인 변은 앞쪽 몫만 그림 (ProjectSegmentToView).
// 이 기능 전의 캐스케이드 와이어는 그런 변을 통째로 건너뛰었음.
//   흰 선      고정 카메라의 전체 절두체 (objects·lights 항목 중 하나라도 켜져 있을 때. 캐스케이드만 고정하면 예전 화면 그대로)
//   색 선      캐스케이드 조각마다 (0 빨강, 1 초록, 2 파랑, 3 노랑 — 디버그 뷰 8 의 색과 같음). cascades 항목이 켜져 있을 때
void Editor::DrawFreezeOverlay(Engine& engine, float x, float y, float width, float height)
{
	if (playState != PlayState::Editing || width <= 0.0f || height <= 0.0f) return;
	const Renderer& renderer = engine.GetRenderer();
	if (!renderer.IsCullFrozen()) return;
	const Renderer::CullFreezeParts parts = renderer.GetCullFreezeParts();

	const Camera& camera = engine.GetCamera();
	const XMMATRIX viewProj = camera.GetViewMatrix() * camera.GetProjectionMatrix();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);
	// 꼭짓점 순서 [0..3] 앞면 (−x−y, +x−y, −x+y, +x+y), [4..7] 뒷면 같은 순서 (GetCascadeFrustumCorners 와 같음).
	// 앞면 사각형 (0,1,3,2), 뒷면 사각형 (4,5,7,6), 앞뒤를 잇는 변 i → i+4
	static const int edges[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
	const auto drawBox = [&](const XMFLOAT3* corners, ImU32 color, float thickness)
	{
		for (const auto& edge : edges)
		{
			ImVec2 a, b;
			if (ProjectSegmentToView(viewProj, x, y, width, height, corners[edge[0]], corners[edge[1]], a, b))
			{
				dl->AddLine(a, b, color, thickness);
			}
		}
	};

	if (parts.objects || parts.lights)
	{
		// 고정 카메라 ViewProj 의 역행렬로 NDC 상자 (x, y = ±1, D3D z = 0·1) 꼭짓점을 월드로 되돌림. 원근·직교 둘 다 같은 식.
		const Camera& frozen = renderer.GetCullFreezeCamera();
		const XMMATRIX inverse = XMMatrixInverse(nullptr, frozen.GetViewMatrix() * frozen.GetProjectionMatrix());
		XMFLOAT3 corners[8];
		for (int k = 0; k < 8; ++k)
		{
			const XMVECTOR ndc = XMVectorSet((k & 1) ? 1.0f : -1.0f, (k & 2) ? 1.0f : -1.0f, (k & 4) ? 1.0f : 0.0f, 1.0f);
			XMStoreFloat3(&corners[k], XMVector3TransformCoord(ndc, inverse));
		}
		drawBox(corners, IM_COL32(255, 255, 255, 230), 1.5f);
	}

	if (parts.cascades)
	{
		static const ImU32 colors[4] = { IM_COL32(255, 60, 60, 255), IM_COL32(60, 255, 60, 255), IM_COL32(80, 120, 255, 255), IM_COL32(255, 255, 60, 255) };
		const uint32_t count = renderer.GetCascadeFrustumCount();
		for (uint32_t c = 0; c < count && c < 4; ++c)
		{
			drawBox(renderer.GetCascadeFrustumCorners(c), colors[c], 2.0f);
		}
	}
	dl->PopClipRect();
}

void Editor::DrawLightIcons(Engine& engine, float x, float y, float width, float height)
{
	if (playState != PlayState::Editing || width <= 0.0f || height <= 0.0f) return;
	const Camera& camera = engine.GetCamera();
	const std::vector<Light>& lights = engine.GetScene().GetLights();
	const XMMATRIX viewProj = camera.GetViewMatrix() * camera.GetProjectionMatrix();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);
	const auto project = [&](const XMFLOAT3& p, ImVec2& out) -> bool
	{
		return ProjectToView(viewProj, x, y, width, height, p, out);
	};

	for (int i = 0; i < static_cast<int>(lights.size()); ++i)
	{
		if (m_lightIconMode == LightIconMode::SelectedOnly && i != m_selectedLight) continue;
		const Light& light = lights[i];
		const XMFLOAT3 anchor = LightAnchor(camera, lights, i, x, y, width, height);
		ImVec2 c;
		if (!project(anchor, c)) continue;
		const bool selected = i == m_selectedLight;
		// 컬링 고정 디버그: 고정 시점에서 컬링된 지역광은 회색 + X (빛도 꺼져 있음 — Renderer::CullLights)
		const bool culledByFreeze = engine.GetRenderer().IsLightCulledByFreeze(static_cast<uint32_t>(i));
		const ImU32 tint = culledByFreeze ? IM_COL32(110, 110, 110, 255) : IM_COL32(static_cast<int>(std::clamp(light.color.x, 0.0f, 1.0f) * 255), static_cast<int>(std::clamp(light.color.y, 0.0f, 1.0f) * 255), static_cast<int>(std::clamp(light.color.z, 0.0f, 1.0f) * 255), 255);
		const ImU32 outline = selected ? IM_COL32(255, 200, 40, 255) : IM_COL32(20, 20, 20, 230);
		const float thickness = selected ? 2.5f : 1.5f;

		const XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&light.direction));
		const auto worldLine = [&](const XMVECTOR& a, const XMVECTOR& b, ImU32 color, float t)
		{
			XMFLOAT3 fa, fb;
			ImVec2 pa, pb;
			XMStoreFloat3(&fa, a);
			XMStoreFloat3(&fb, b);
			if (project(fa, pa) && project(fb, pb))
			{
				dl->AddLine(pa, pb, color, t);
			}
		};

		dl->AddCircleFilled(c, 7.0f, tint);
		dl->AddCircle(c, 7.0f, outline, 0, thickness);
		if (culledByFreeze)
		{
			dl->AddLine(ImVec2(c.x - 4.5f, c.y - 4.5f), ImVec2(c.x + 4.5f, c.y + 4.5f), IM_COL32(30, 30, 30, 255), 2.0f);
			dl->AddLine(ImVec2(c.x - 4.5f, c.y + 4.5f), ImVec2(c.x + 4.5f, c.y - 4.5f), IM_COL32(30, 30, 30, 255), 2.0f);
		}
		if (light.type == LightType::Point)
		{
			for (int r = 0; r < 8; ++r)   // 사방으로 뻗는 짧은 광선 (화면 고정 크기)
			{
				const float a = r * XM_PIDIV4;
				dl->AddLine(ImVec2(c.x + cosf(a) * 10.0f, c.y + sinf(a) * 10.0f), ImVec2(c.x + cosf(a) * 14.0f, c.y + sinf(a) * 14.0f), outline, thickness);
			}
		}
		else if (light.type == LightType::Spot)
		{
			// 월드 크기의 원뿔: 길이 = min(range, 3), 밑면 반지름은 바깥 원뿔각에서 구함
			const float length = (std::min)(light.range, 3.0f);
			const float radius = length * tanf(acosf((std::clamp)(light.outerConeCos, 0.0f, 0.9999f)));
			const XMVECTOR refUp = fabsf(XMVectorGetY(dir)) > 0.99f ? XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f) : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			const XMVECTOR right = XMVector3Normalize(XMVector3Cross(refUp, dir));
			const XMVECTOR up = XMVector3Cross(dir, right);
			const XMVECTOR apex = XMLoadFloat3(&anchor);
			const XMVECTOR center = apex + dir * length;
			XMVECTOR prev = center + right * radius;
			for (int s = 1; s <= 16; ++s)
			{
				const float a = s * XM_2PI / 16.0f;
				const XMVECTOR cur = center + right * (cosf(a) * radius) + up * (sinf(a) * radius);
				worldLine(prev, cur, outline, thickness);
				if (s % 4 == 0)
				{
					worldLine(apex, cur, outline, thickness);
				}
				prev = cur;
			}
		}
		else
		{
			// Directional: 해 아이콘 + 진행 방향 화살표. 위치가 없으므로 화면 고정 자리에 있음 (회전 기즈모만 붙음).
			const XMVECTOR a = XMLoadFloat3(&anchor);
			const XMVECTOR b = a + dir * 1.5f;
			worldLine(a, b, outline, thickness + 0.5f);
			XMFLOAT3 tip;
			XMStoreFloat3(&tip, b);
			ImVec2 pt;
			if (project(tip, pt))
			{
				dl->AddCircleFilled(pt, 3.5f, outline);
				dl->AddText(ImVec2(c.x + 12.0f, c.y - 8.0f), outline, "Dir");
			}
		}
		char label[16];
		snprintf(label, sizeof(label), "L%d", i);
		dl->AddText(ImVec2(c.x + 10.0f, c.y + 6.0f), selected ? IM_COL32(255, 200, 40, 255) : IM_COL32(230, 230, 230, 230), label);
	}
	dl->PopClipRect();
}

void Editor::DrawLightGizmo(Engine& engine, float x, float y, float width, float height)
{
	Scene& scene = engine.GetScene();
	if (playState != PlayState::Editing || m_selectedLight < 0 || m_selectedLight >= static_cast<int>(scene.GetLights().size()) || width <= 0.0f || height <= 0.0f) return;
	const Camera& camera = engine.GetCamera();
	Light& light = scene.GetLights()[m_selectedLight];
	const bool directional = light.type == LightType::Directional;
	const bool spot = light.type == LightType::Spot;
	// Directional = 회전만 (위치 없음). Spot = 툴바의 이동/회전. Point = 이동만 (방향이 없음).
	const bool rotate = directional || (spot && m_gizmoOperation == 1);

	XMFLOAT4X4 view, proj;
	XMStoreFloat4x4(&view, camera.GetViewMatrix());
	XMStoreFloat4x4(&proj, camera.GetProjectionMatrix());
	const XMFLOAT3 anchor = LightAnchor(camera, scene.GetLights(), m_selectedLight, x, y, width, height);
	XMFLOAT4X4 world = LightGizmoMatrix(light, anchor, directional || spot);

	ImGuizmo::SetOrthographic(!camera.IsUsePerspectiveProjection());
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(x, y, width, height);
	const ImGuizmo::MODE mode = (m_gizmoWorld && !directional) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
	if (ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], rotate ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE, mode, &world.m[0][0]))
	{
		if (rotate)
		{
			const XMVECTOR f = XMVectorSet(world.m[2][0], world.m[2][1], world.m[2][2], 0.0f);
			if (XMVectorGetX(XMVector3LengthSq(f)) > 1e-8f)
			{
				XMStoreFloat3(&light.direction, XMVector3Normalize(f));
			}
		}
		else
		{
			light.position = XMFLOAT3(world.m[3][0], world.m[3][1], world.m[3][2]);
		}
	}
	if (ImGuizmo::IsUsing())
	{
		m_gizmoUsing = true;   // TrackHistory 가 busy 로 보고, 끝난 프레임에 한 단계를 기록함
	}
}

void Editor::SyncSelection(Scene& scene)
{
	// 조명 선택: 되돌리기·삭제로 개수가 줄었으면 해제. 조명이 새로 골라졌으면(씬 뷰·Lights 패널) 오브젝트
	// 선택을 비우고, 반대도 마찬가지.
	if (m_selectedLight >= static_cast<int>(scene.GetLights().size()))
	{
		m_selectedLight = -1;
	}
	if (m_selectedLight >= 0 && m_selectedLight != m_selectedLightPrev)
	{
		m_selected = -1;
		m_selectedPrev = -2;
	}
	else if (m_selected >= 0 && m_selected != m_selectedPrev)
	{
		m_selectedLight = -1;
	}
	m_selectedLightPrev = m_selectedLight;

	if (m_selected != m_selectedPrev)
	{
		// 지난 프레임 이후 UI(또는 앱)가 인덱스를 바꿈 → 그 오브젝트가 새 선택
		const GameObject* object = scene.GetObject(m_selected < 0 ? SIZE_MAX : static_cast<size_t>(m_selected));
		m_selectedId = object != nullptr ? object->GetId() : 0;
		if (object == nullptr)
		{
			m_selected = -1;
		}
	}
	else if (m_selectedId != 0)
	{
		// 인덱스는 그대로인데 오브젝트 벡터가 바뀌었을 수 있음 → ID 로 다시 찾음 (지워졌으면 선택 해제)
		const GameObject* object = scene.FindById(m_selectedId);
		m_selected = object != nullptr ? static_cast<int>(scene.IndexOf(object)) : -1;
		if (object == nullptr)
		{
			m_selectedId = 0;
		}
	}
	m_selectedPrev = m_selected;
}

namespace
{
	// 재질 하나의 편집 위젯. R 버튼의 기본값 = 재질 멤버 초기값.
	void DrawMaterialFields(Material& material)
	{
		static const Material kDefaults;
		ImGui::ColorEdit4("Base color", &material.baseColor.x);
		DebugUI::SliderFloatR("Metallic", &material.metallic, kDefaults.metallic, 0.0f, 1.0f);   // PBR
		DebugUI::SliderFloatR("Roughness", &material.roughness, kDefaults.roughness, 0.0f, 1.0f);
		DebugUI::SliderFloatR("IOR", &material.ior, kDefaults.ior, 1.0f, 3.0f);                              // KHR_materials_ior. Blender 의 Specular 는 ior 1000 + 반사 색으로 옴
		DebugUI::SliderFloatR("Specular", &material.specularFactor, kDefaults.specularFactor, 0.0f, 1.0f);   // KHR_materials_specular
		ImGui::ColorEdit3("Specular color", &material.specularColor.x);
		DebugUI::SliderFloatR("Clearcoat", &material.clearcoatFactor, kDefaults.clearcoatFactor, 0.0f, 1.0f);   // KHR_materials_clearcoat
		DebugUI::SliderFloatR("Clearcoat roughness", &material.clearcoatRoughness, kDefaults.clearcoatRoughness, 0.0f, 1.0f);
		ImGui::ColorEdit3("Emissive", &material.emissive.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
		DebugUI::DragFloat2R("UV scale", &material.uvScale.x, kDefaults.uvScale.x, kDefaults.uvScale.y, 0.1f, 0.1f, 64.0f);
		DebugUI::SliderFloatR("Normal strength", &material.normalStrength, kDefaults.normalStrength, 0.0f, 5.0f);
		DebugUI::SliderFloatR("Transmission", &material.transmissionFactor, kDefaults.transmissionFactor, 0.0f, 1.0f);   // KHR_materials_transmission (알파 블렌딩으로 근사)
		DebugUI::SliderFloatR("Alpha cutoff", &material.alphaCutoff, kDefaults.alphaCutoff, 0.0f, 1.0f);
		DebugUI::DrawDissolveNoiseFields(material);
		ImGui::Checkbox("Alpha blend", &material.alphaBlend);
		ImGui::Checkbox("Unlit", &material.unlit);
		DebugUI::SameLineIfFits(DebugUI::CheckboxWidth("Wireframe"));
		ImGui::Checkbox("Wireframe", &material.wireframe);
		DebugUI::SameLineIfFits(DebugUI::CheckboxWidth("Double sided"));
		ImGui::Checkbox("Double sided", &material.doubleSided);
		ImGui::TextDisabled("albedo: %s   normal: %s", material.albedoTexture.empty() ? "(none)" : material.albedoTexture.c_str(), material.normalTexture.empty() ? "(none)" : material.normalTexture.c_str());
	}

	// before → after 에서 바뀐 필드만 target 에 복사함. "모든 재질" 편집기가 한 값만 바꿔도 나머지 재질의 다른 값은 그대로 두기 위함.
	void ApplyChangedFields(const Material& before, const Material& after, Material& target)
	{
		if (before.baseColor.x != after.baseColor.x || before.baseColor.y != after.baseColor.y || before.baseColor.z != after.baseColor.z || before.baseColor.w != after.baseColor.w)
		{
			target.baseColor = after.baseColor;
		}
		if (before.emissive.x != after.emissive.x || before.emissive.y != after.emissive.y || before.emissive.z != after.emissive.z)
		{
			target.emissive = after.emissive;
		}
		if (before.uvScale.x != after.uvScale.x || before.uvScale.y != after.uvScale.y)
		{
			target.uvScale = after.uvScale;
		}
		if (before.metallic != after.metallic)
		{
			target.metallic = after.metallic;
		}
		if (before.roughness != after.roughness)
		{
			target.roughness = after.roughness;
		}
		if (before.ior != after.ior)
		{
			target.ior = after.ior;
		}
		if (before.clearcoatFactor != after.clearcoatFactor)
		{
			target.clearcoatFactor = after.clearcoatFactor;
		}
		if (before.clearcoatRoughness != after.clearcoatRoughness)
		{
			target.clearcoatRoughness = after.clearcoatRoughness;
		}
		if (before.specularFactor != after.specularFactor)
		{
			target.specularFactor = after.specularFactor;
		}
		if (before.specularColor.x != after.specularColor.x || before.specularColor.y != after.specularColor.y || before.specularColor.z != after.specularColor.z)
		{
			target.specularColor = after.specularColor;
		}
		if (before.normalStrength != after.normalStrength)
		{
			target.normalStrength = after.normalStrength;
		}
		if (before.alphaCutoff != after.alphaCutoff)
		{
			target.alphaCutoff = after.alphaCutoff;
		}
		if (before.transmissionFactor != after.transmissionFactor)
		{
			target.transmissionFactor = after.transmissionFactor;
		}
		if (before.alphaBlend != after.alphaBlend)
		{
			target.alphaBlend = after.alphaBlend;
		}
		if (before.dissolveNoiseTexture != after.dissolveNoiseTexture)
		{
			target.dissolveNoiseTexture = after.dissolveNoiseTexture;
		}
		if (before.dissolveNoiseScale != after.dissolveNoiseScale)
		{
			target.dissolveNoiseScale = after.dissolveNoiseScale;
		}
		if (before.unlit != after.unlit)
		{
			target.unlit = after.unlit;
		}
		if (before.wireframe != after.wireframe)
		{
			target.wireframe = after.wireframe;
		}
		if (before.doubleSided != after.doubleSided)
		{
			target.doubleSided = after.doubleSided;
		}
	}

	// 이 오브젝트와 자손이 실제로 그리는 모델 슬롯 재질(중복 제거). 슬롯 재질이 있는 오브젝트만 대상임.
	void CollectSlotMaterials(Scene& scene, const GameObject& object, std::vector<Material*>& out)
	{
		if (object.GetMesh() != nullptr && !object.GetSlotMaterials().empty())
		{
			for (const Submesh& submesh : object.GetMesh()->GetSubmeshes())
			{
				const Material* used = object.GetMaterialForSlot(submesh.materialSlot);
				if (used == nullptr) continue;
				for (auto& owned : scene.GetMaterials())
				{
					if (owned.get() == used && std::find(out.begin(), out.end(), owned.get()) == out.end())
					{
						out.push_back(owned.get());
					}
				}
			}
		}
		for (const GameObject* child : object.GetChildren())
		{
			CollectSlotMaterials(scene, *child, out);
		}
	}

	// 모델 오브젝트(또는 모델 루트)의 인스펙터 재질 영역. 위쪽 "Material" 콤보는 오브젝트 자체 재질(모델에서는 쓰이지 않는
	// 기본 슬롯)이라 여기서 고쳐도 화면이 바뀌지 않았음. 그려지는 재질은 서브메시 슬롯의 것이므로 여기서 편집함.
	void DrawModelMaterials(Scene& scene, const GameObject& object, Renderer& renderer)
	{
		std::vector<Material*> materials;
		CollectSlotMaterials(scene, object, materials);
		if (materials.empty()) return;

		ImGui::SeparatorText("Model materials");
		ImGui::TextDisabled("%zu materials used by this object and its children (shared with other instances of the model)", materials.size());
		if (materials.size() > 1 && ImGui::TreeNodeEx("All materials", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("a changed value is applied to all %zu materials", materials.size());
			const Material before = *materials[0];   // 표시는 첫 재질의 값. 바뀐 필드만 모두에게 적용함
			Material edited = before;
			DrawMaterialFields(edited);
			for (Material* material : materials)
			{
				ApplyChangedFields(before, edited, *material);
			}
			ImGui::TreePop();
		}
		for (Material* material : materials)
		{
			ImGui::PushID(material);
			if (ImGui::TreeNode("material", "%s", material->name.c_str()))
			{
				DrawMaterialFields(*material);
				DebugUI::DrawTextureImportSettings(*material, renderer);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
	}
}

void Editor::DrawInspector(Engine& engine)
{
	DebugUI::BeginPanel("Inspector");
	Scene& scene = engine.GetScene();
	if (m_selected < 0 || m_selected >= static_cast<int>(scene.GetObjects().size()))
	{
		if (m_selectedLight >= 0)
		{
			ImGui::TextDisabled("Light %d selected: move/rotate it in the Scene view, edit it in the Lights panel.", m_selectedLight);
		}
		else
		{
			ImGui::TextDisabled("Click an object in the Scene view or the Hierarchy.");
		}
		DebugUI::EndPanel();
		return;
	}
	GameObject& object = *scene.GetObjects()[m_selected];
	ImGui::Text("%s", object.GetName().c_str());
	ImGui::TextDisabled("id %llu%s%s", static_cast<unsigned long long>(object.GetId()), object.GetPrefabSource().empty() ? "" : "   prefab ", object.GetPrefabSource().c_str());
	if (playState != PlayState::Editing)
	{
		ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.15f, 1.0f), "Playing: edits are discarded on Stop");
	}
	ImGui::Separator();

	Transform& t = object.GetTransform();
	XMFLOAT3 position = t.GetPosition();
	XMFLOAT3 rotation = t.GetRotation();
	XMFLOAT3 scale = t.GetScale();
	XMFLOAT3 rotationDeg(XMConvertToDegrees(rotation.x), XMConvertToDegrees(rotation.y), XMConvertToDegrees(rotation.z));
	const bool isUI = UISystem::IsUnderCanvas(object);
	if (isUI)
	{
		// 화면 공간 캔버스 아래: 위치·크기는 RectTransform, Transform 은 피벗 중심 회전 z 와 스케일 xy 만 씀
		ImGui::TextDisabled("UI element: position/size in RectTransform; rotation z and scale xy turn around the pivot");
	}
	else if (DebugUI::DragFloat3R("Position", &position.x, 0.0f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f))
	{
		t.SetPosition(position);
	}
	if (DebugUI::DragFloat3R("Rotation (deg)", &rotationDeg.x, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f))
	{
		t.SetRotation(XMFLOAT3(XMConvertToRadians(rotationDeg.x), XMConvertToRadians(rotationDeg.y), XMConvertToRadians(rotationDeg.z)));
	}
	if (DebugUI::DragFloat3R("Scale", &scale.x, 1.0f, 1.0f, 1.0f, 0.01f, 0.001f, 1000.0f))
	{
		t.SetScale(scale);
	}
	if (t.HasPreTransform())
	{
		ImGui::TextDisabled("(model node pre-transform applied before S*R*T)");
	}
	// 부모가 있으면 위 값은 부모 기준 로컬임을 알리고 월드 위치를 함께 보임. Detach 는 월드 자세를 유지한 채
	// 루트로 올림.
	if (const GameObject* parent = object.GetParent())
	{
		const XMFLOAT3 wp = object.GetWorldPosition();
		ImGui::TextDisabled("local to parent '%s'   world (%.2f, %.2f, %.2f)", parent->GetName().c_str(), wp.x, wp.y, wp.z);
		ImGui::SameLine();
		if (ImGui::SmallButton("Detach"))
		{
			object.SetParent(nullptr);
		}
	}
	else if (!object.GetChildren().empty())
	{
		ImGui::TextDisabled("%zu children follow this object (drag in Hierarchy to reparent)", object.GetChildren().size());
	}

	// 카메라 오브젝트 — 에디터 시점과 자세를 주고받음
	if (CameraComponent* cameraComponent = object.GetBehaviour<CameraComponent>())
	{
		if (ImGui::Button("Look through"))
		{
			cameraComponent->ApplyTo(engine.GetCamera(), false);   // 에디터 시점 ← 이 카메라 (렌즈는 그대로)
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("move the editor view to this camera");
		}
		ImGui::SameLine();
		if (ImGui::Button("Align to view"))
		{
			cameraComponent->SetFromCamera(engine.GetCamera());   // 이 카메라 ← 에디터 시점
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("place this camera where the editor view is");
		}
		if (scene.GetActiveCamera() == cameraComponent)
		{
			ImGui::TextColored(ImVec4(0.3f, 0.75f, 0.35f, 1.0f), "active camera");
		}
		else if (playState == PlayState::Editing)
		{
			ImGui::TextDisabled("rendered through during Play (highest priority wins)");
		}
	}

	if (const Mesh* mesh = object.GetMesh())
	{
		const MeshSource* source = scene.GetMeshSource(mesh);
		const char* type = "custom";
		if (source != nullptr)
		{
			switch (source->type)
			{
			case MeshSource::Type::Sphere:   type = "sphere"; break;
			case MeshSource::Type::Cube:     type = "cube"; break;
			case MeshSource::Type::Plane:    type = "plane"; break;
			case MeshSource::Type::Cylinder: type = "cylinder"; break;
			case MeshSource::Type::Model:    type = "model"; break;
			default:                         break;
			}
		}
		ImGui::Separator();
		ImGui::TextWrapped("Mesh: %s   vertices %u   triangles %u   submeshes %zu", type, mesh->GetVertexCount(), mesh->GetIndexCount() / 3, mesh->GetSubmeshes().size());
		if (source != nullptr && source->type == MeshSource::Type::Model)
		{
			ImGui::TextDisabled("%s #%u", Log::ToUtf8(source->modelPath.c_str()).c_str(), source->meshIndex);
		}
	}

	// 재질: 씬의 재질 중에서 고르고 그 자리에서 편집함.
	ImGui::Separator();
	std::vector<std::unique_ptr<Material>>& materials = scene.GetMaterials();
	int current = -1;
	for (int i = 0; i < static_cast<int>(materials.size()); ++i)
	{
		if (materials[i].get() == object.GetMaterial())
		{
			current = i;
		}
	}
	const char* currentName = current >= 0 ? materials[current]->name.c_str() : "(renderer default)";
	if (ImGui::BeginCombo("Material", currentName))
	{
		for (int i = 0; i < static_cast<int>(materials.size()); ++i)
		{
			ImGui::PushID(i);
			if (ImGui::Selectable(materials[i]->name.c_str(), i == current))
			{
				object.SetMaterial(materials[i].get());
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	DrawModelMaterials(scene, object, engine.GetRenderer());
	if (!object.GetSlotMaterials().empty())
	{
		ImGui::TextDisabled("submesh slots: %zu", object.GetSlotMaterials().size());
		// 모델 재질은 같은 모델을 놓은 모든 오브젝트가 공유함 — 편집하면 모두 바뀜. 이 오브젝트만 다르게
		// 하려면 슬롯별로 분리함.
		if (object.GetMesh() != nullptr && ImGui::TreeNode("Shared slot materials"))
		{
			std::vector<uint32_t> usedSlots;
			for (const Submesh& submesh : object.GetMesh()->GetSubmeshes())
			{
				if (std::find(usedSlots.begin(), usedSlots.end(), submesh.materialSlot) == usedSlots.end())
				{
					usedSlots.push_back(submesh.materialSlot);
				}
			}
			for (uint32_t slot : usedSlots)
			{
				const Material* slotMaterial = object.GetMaterialForSlot(slot);
				if (slotMaterial == nullptr) continue;
				const size_t users = scene.CountMaterialUsers(slotMaterial);
				ImGui::PushID(static_cast<int>(slot));
				ImGui::Text("#%u %s", slot, slotMaterial->name.c_str());
				ImGui::SameLine();
				if (users > 1)
				{
					ImGui::TextDisabled("(shared by %zu)", users);
					ImGui::SameLine();
					if (ImGui::SmallButton("Make unique"))
					{
						Material* unique = scene.CloneMaterialUnique(*slotMaterial);
						std::vector<const Material*> slots = object.GetSlotMaterials();
						if (slot < slots.size())
						{
							slots[slot] = unique;
						}
						object.SetSlotMaterials(std::move(slots));
					}
				}
				else
				{
					ImGui::TextDisabled("(unique)");
				}
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
	}
	if (current >= 0 && object.GetSlotMaterials().empty())   // 모델 오브젝트는 슬롯 재질로 그려지므로 위 DrawModelMaterials 에서 편집함
	{
		DrawMaterialFields(*materials[current]);
	}

	// 컴포넌트
	ImGui::Separator();
	DrawComponents(engine, object);
	DebugUI::EndPanel();
}

// ------------------------------------------------------------------ 재생 · 컴포넌트

namespace
{
	// Behaviour::Reflect 의 필드를 ImGui 위젯으로 그림. Scene 계층은 ImGui 를 모르므로 방문자는 에디터 쪽에 둠.
	class ImGuiPropertyVisitor : public PropertyVisitor
	{
	public:
		ImGuiPropertyVisitor(GameObject* owner = nullptr, Scene* scene = nullptr)
			: m_owner(owner), m_scene(scene)
		{}
		// 오브젝트 상대 경로 칸 + Hierarchy 에서 끌어 놓기 (놓으면 이 컴포넌트의 오브젝트 기준 경로로 바꿈). 못 찾으면 빨간 표시
		void ObjectRef(const char* name, std::string& value) override
		{
			char buffer[256];
			strncpy_s(buffer, value.c_str(), _TRUNCATE);
			if (ImGui::InputTextWithHint(name, "drop an object from Hierarchy, or path (. .. Child/Name)", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				value = buffer;
			}
			DropObject(value);
			if (m_owner != nullptr && !value.empty() && UIObjectRef::Resolve(*m_owner, value) == nullptr)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "missing");
			}
		}
		// UIEvent 의 저장되는 연결 목록 (유니티 UnityEvent 의 Inspector): 대상 · 함수 · 인자
		void Event(const char* name, UIEventBase& value) override
		{
			ImGui::PushID(name);
			ImGui::TextUnformatted(name);
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)", UIValueKindName(value.GetValueKind()));
			ImGui::SameLine();
			if (ImGui::SmallButton("+"))
			{
				value.calls.push_back(UIPersistentCall{ ".", "", true, {} });
			}
			int removeAt = -1;
			for (int i = 0; i < static_cast<int>(value.calls.size()); ++i)
			{
				UIPersistentCall& call = value.calls[i];
				ImGui::PushID(i);
				ImGui::Indent();
				if (ImGui::SmallButton("x"))
				{
					removeAt = i;
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
				char buffer[256];
				strncpy_s(buffer, call.target.c_str(), _TRUNCATE);
				if (ImGui::InputTextWithHint("##target", "target (drop object)", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
				{
					call.target = buffer;
				}
				DropObject(call.target);
				ImGui::SameLine();
				// 대상 오브젝트의 컴포넌트마다 SHERLOCK_UI_ACTION 으로 올린 함수
				GameObject* target = m_owner != nullptr ? UIObjectRef::Resolve(*m_owner, call.target) : nullptr;
				const UIActionRegistry::Action* current = UIActionRegistry::Find(call.method);
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::BeginCombo("##method", call.method.empty() ? "(no function)" : call.method.c_str()))
				{
					if (target == nullptr)
					{
						ImGui::TextDisabled("target not found");
					}
					else
					{
						for (const auto& b : target->GetBehaviours())
						{
							for (const UIActionRegistry::Action* action : UIActionRegistry::ListFor(b->GetTypeName()))
							{
								const std::string label = action->fullName + " (" + UIValueKindName(action->argument) + ")";
								if (ImGui::Selectable(label.c_str(), action->fullName == call.method))
								{
									call.method = action->fullName;
									call.dynamic = action->argument == value.GetValueKind() && action->argument != UIValue::None;
									call.argument = UIValue{};
									call.argument.kind = action->argument;
								}
							}
						}
					}
					ImGui::EndCombo();
				}
				if (current == nullptr && !call.method.empty())
				{
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "function not registered (script DLL not loaded?)");
				}
				else if (current != nullptr && current->argument != UIValue::None)
				{
					// 이벤트 값을 그대로 넘길 수 있으면 (종류가 같음) dynamic 선택, 아니면 고정 인자
					const bool canDynamic = current->argument == value.GetValueKind();
					if (canDynamic)
					{
						ImGui::Checkbox("dynamic (use event value)", &call.dynamic);
					}
					else
					{
						call.dynamic = false;
					}
					if (!call.dynamic)
					{
						call.argument.kind = current->argument;
						switch (current->argument)
						{
						case UIValue::Float: ImGui::DragFloat("argument", &call.argument.f, 0.01f); break;
						case UIValue::Int: ImGui::DragInt("argument", &call.argument.i); break;
						case UIValue::Bool: ImGui::Checkbox("argument", &call.argument.b); break;
						case UIValue::String:
						{
							char text[256];
							strncpy_s(text, call.argument.s.c_str(), _TRUNCATE);
							if (ImGui::InputText("argument", text, sizeof(text)))
							{
								call.argument.s = text;
							}
							break;
						}
						default: break;
						}
					}
				}
				ImGui::Unindent();
				ImGui::PopID();
			}
			if (removeAt >= 0)
			{
				value.calls.erase(value.calls.begin() + removeAt);
			}
			ImGui::PopID();
		}
		void Float(const char* name, float& value, float speed) override { ImGui::DragFloat(name, &value, speed); }
		void Int(const char* name, int& value) override { ImGui::DragInt(name, &value); }
		void Bool(const char* name, bool& value) override { ImGui::Checkbox(name, &value); }
		void Float3(const char* name, XMFLOAT3& value, float speed) override { ImGui::DragFloat3(name, &value.x, speed); }
		void Float2(const char* name, XMFLOAT2& value, float speed) override { ImGui::DragFloat2(name, &value.x, speed); }
		void Color(const char* name, XMFLOAT4& value) override { ImGui::ColorEdit4(name, &value.x, ImGuiColorEditFlags_AlphaBar); }
		void Enum(const char* name, int& value, const char* const* names, int count) override
		{
			const char* preview = value >= 0 && value < count ? names[value] : "?";
			if (ImGui::BeginCombo(name, preview))
			{
				for (int i = 0; i < count; ++i)
				{
					if (ImGui::Selectable(names[i], i == value))
					{
						value = i;
					}
				}
				ImGui::EndCombo();
			}
		}
		void String(const char* name, std::string& value) override
		{
			char buffer[256];
			strncpy_s(buffer, value.c_str(), sizeof(buffer) - 1);
			if (ImGui::InputText(name, buffer, sizeof(buffer)))
			{
				value = buffer;
			}
		}
		// 에셋 이름 칸 + 콘텐츠 브라우저 드롭. 이름 규칙은 재질 텍스처와 같음 (Textures\ 바로 아래는 이름만, 그 밖은 "asset:<Assets 기준>")
		void MultilineString(const char* name, std::string& value) override
		{
			// 한글 입력 가능 (ImGui 가 WM_CHAR 의 UTF-16 을 받음). 4 KB 까지
			char buffer[4096];
			strncpy_s(buffer, value.c_str(), _TRUNCATE);
			if (ImGui::InputTextMultiline(name, buffer, sizeof(buffer), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f)))
			{
				value = buffer;
			}
		}
		void Asset(const char* name, std::string& value, AssetKind kind) override
		{
			// Enter 로만 적용 (글자마다 적용하면 칠 때마다 반쯤 친 이름을 읽으려다 실패 경고가 남음 — 실패도 캐시에 남음)
			char buffer[260];
			strncpy_s(buffer, value.c_str(), _TRUNCATE);
			if (ImGui::InputTextWithHint(name, kind == AssetKind::Font ? "default (Noto Sans KR) or drop a .ttf/.otf, Enter" : "name or drop a texture, Enter to apply", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				value = buffer;
			}
			if (!ImGui::BeginDragDropTarget()) return;
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayloadType);
			if (payload != nullptr && payload->DataSize == sizeof(AssetDragPayload))
			{
				AssetDragPayload asset = *static_cast<const AssetDragPayload*>(payload->Data);
				asset.relativePath[259] = L'\0';   // 종료 문자 방어
				if (kind == AssetKind::Texture && asset.type == AssetType::Texture)
				{
					value = ContentBrowser::ToMaterialTextureName(asset.relativePath);
				}
				else if (kind == AssetKind::Font && asset.type == AssetType::Font)
				{
					value = "asset:" + Log::ToUtf8(asset.relativePath);   // FontLibrary 이름 규칙: Assets 기준 경로
				}
			}
			ImGui::EndDragDropTarget();
		}

	private:
		void DropObject(std::string& value)
		{
			if (m_owner == nullptr || m_scene == nullptr || !ImGui::BeginDragDropTarget()) return;
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SHERLOCK_OBJECT"); payload != nullptr && payload->DataSize == sizeof(int))
			{
				const int index = *static_cast<const int*>(payload->Data);
				if (index >= 0 && index < static_cast<int>(m_scene->GetObjects().size()))
				{
					value = UIObjectRef::MakePath(*m_owner, *m_scene->GetObjects()[index]);
				}
			}
			ImGui::EndDragDropTarget();
		}

		GameObject* m_owner;
		Scene* m_scene;
	};

	// RectTransform 앵커 프리셋 (유니티 Anchor Presets 의 Alt+Shift 동작: 앵커·피벗을 정하고 위치를 그 자리에 붙임).
	// 늘이는 축은 sizeDelta 를 0 으로 (부모를 꽉 채움), 아닌 축은 크기를 그대로 둠.
	struct AnchorPreset
	{
		const char* name;
		float minX, minY, maxX, maxY;
	};
	constexpr AnchorPreset kAnchorPresets[] = {
		{ "top left", 0, 1, 0, 1 }, { "top center", 0.5f, 1, 0.5f, 1 }, { "top right", 1, 1, 1, 1 },
		{ "middle left", 0, 0.5f, 0, 0.5f }, { "center", 0.5f, 0.5f, 0.5f, 0.5f }, { "middle right", 1, 0.5f, 1, 0.5f },
		{ "bottom left", 0, 0, 0, 0 }, { "bottom center", 0.5f, 0, 0.5f, 0 }, { "bottom right", 1, 0, 1, 0 },
		{ "stretch top", 0, 1, 1, 1 }, { "stretch middle", 0, 0.5f, 1, 0.5f }, { "stretch bottom", 0, 0, 1, 0 },
		{ "stretch left", 0, 0, 0, 1 }, { "stretch center", 0.5f, 0, 0.5f, 1 }, { "stretch right", 1, 0, 1, 1 },
		{ "stretch all", 0, 0, 1, 1 },
	};

	// 레이아웃 그룹·ContentSizeFitter·Slider 가 정한 값은 Inspector 에서 고쳐도 다음 프레임에 덮임 — 유니티의 "Some values driven by ..." 표시
	void DrawDrivenNote(const UISystem& ui, const GameObject& object)
	{
		UISystem::ElementGeometry g;
		if (!ui.GetElementGeometry(&object, g)) return;
		if (!g.driven[0] && !g.driven[1] && !g.drivenAnchors) return;
		std::string text = "driven:";
		if (g.drivenAnchors) text += " anchors";
		if (g.driven[0]) text += " x/width";
		if (g.driven[1]) text += " y/height";
		ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s (by layout / fitter / slider — edits are overwritten)", text.c_str());
	}

	void DrawAnchorPresets(RectTransform& rt)
	{
		const char* current = "custom";
		for (const AnchorPreset& p : kAnchorPresets)
		{
			if (rt.anchorMin.x == p.minX && rt.anchorMin.y == p.minY && rt.anchorMax.x == p.maxX && rt.anchorMax.y == p.maxY)
			{
				current = p.name;
			}
		}
		if (!ImGui::BeginCombo("anchor preset", current)) return;
		for (const AnchorPreset& p : kAnchorPresets)
		{
			if (!ImGui::Selectable(p.name, p.name == current)) continue;
			const bool stretchX = p.minX != p.maxX;
			const bool stretchY = p.minY != p.maxY;
			rt.anchorMin = XMFLOAT2(p.minX, p.minY);
			rt.anchorMax = XMFLOAT2(p.maxX, p.maxY);
			rt.pivot = XMFLOAT2(stretchX ? 0.5f : p.minX, stretchY ? 0.5f : p.minY);
			rt.anchoredPosition = XMFLOAT2(0.0f, 0.0f);
			if (stretchX)
			{
				rt.sizeDelta.x = 0.0f;
			}
			if (stretchY)
			{
				rt.sizeDelta.y = 0.0f;
			}
		}
		ImGui::EndCombo();
	}
}

void Editor::DrawUISelection(Engine& engine, float x, float y)
{
	Scene& scene = engine.GetScene();
	if (m_selected < 0 || m_selected >= static_cast<int>(scene.GetObjects().size()) || !engine.IsUIVisible()) return;
	XMFLOAT2 quad[4];
	// 지난 프레임의 Build 결과 (이번 프레임 Build 는 OnGUI 뒤). 캔버스 아래가 아니면 false
	if (!engine.GetUI().GetScreenQuad(scene.GetObjects()[m_selected].get(), quad)) return;
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 points[4];
	for (int i = 0; i < 4; ++i)
	{
		points[i] = ImVec2(x + quad[i].x, y + quad[i].y);
	}
	dl->AddPolyline(points, 4, IM_COL32(255, 200, 40, 255), ImDrawFlags_Closed, 1.5f);
}

// ------------------------------------------------------------------ Rect 툴 (7단계) — 도우미는 Hierarchy 앞에 (재부모화가 씀)

bool Editor::ReparentKeepingUI(const UISystem* ui, GameObject& child, GameObject* parent)
{
	const UIKeep keep = child.GetParent() != parent ? CaptureUIKeep(ui, child) : UIKeep{};
	if (child.GetParent() != parent && !child.SetParent(parent)) return false;
	RestoreUIKeep(ui, child, keep);
	return true;
}

Editor::ToolMouse Editor::GetToolMouse() const
{
	ToolMouse m;
	if (m_toolPointer.active)
	{
		m.x = m_toolPointer.x;
		m.y = m_toolPointer.y;
		m.down = m_toolPointer.down;
		m.pressed = m_toolPointer.down && !m_toolPointer.prevDown;
		m.released = !m_toolPointer.down && m_toolPointer.prevDown;
		m.shift = m_toolPointer.shift;
		m.alt = m_toolPointer.alt;
		m.ctrl = m_toolPointer.ctrl;
		return m;
	}
	const ImGuiIO& io = ImGui::GetIO();
	m.x = io.MousePos.x - m_sceneImageX;
	m.y = io.MousePos.y - m_sceneImageY;
	m.down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	m.pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	m.released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
	m.shift = io.KeyShift;
	m.alt = io.KeyAlt;
	m.ctrl = io.KeyCtrl;
	return m;
}

bool Editor::PickUI(Engine& engine, float px, float py)
{
	Scene& scene = engine.GetScene();
	const UISystem& ui = engine.GetUI();
	// 같은 자리(3 px)를 다시 누르면 지금 고른 것 아래의 요소 (유니티 씬 뷰와 같음)
	uint64_t after = 0;
	if (std::fabs(px - m_lastUIPickX) < 3.0f && std::fabs(py - m_lastUIPickY) < 3.0f)
	{
		if (const GameObject* selected = scene.GetObject(m_selected < 0 ? SIZE_MAX : static_cast<size_t>(m_selected)))
		{
			after = selected->GetId();
		}
	}
	m_lastUIPickX = px;
	m_lastUIPickY = py;
	const uint64_t id = ui.PickElement(px, py, after);
	if (id == 0) return false;
	const auto& objects = scene.GetObjects();
	for (int i = 0; i < static_cast<int>(objects.size()); ++i)
	{
		if (objects[i]->GetId() == id)
		{
			m_selected = i;
			m_selectedLight = -1;
			return true;
		}
	}
	return false;
}

bool Editor::DrawRectTool(Engine& engine, float x, float y, bool editing)
{
	const ToolMouse mouse = GetToolMouse();
	Scene& scene = engine.GetScene();
	UISystem& ui = engine.GetUI();
	if (m_rectDrag.active && !mouse.down)
	{
		// 끌지 않고 뗀 안쪽 클릭 = 선택 (겹친 요소를 차례로 고르기)
		const bool click = m_rectDrag.mode == 0 && !m_rectDrag.moved;
		m_rectDrag.active = false;
		if (click && engine.IsUIVisible())
		{
			PickUI(engine, mouse.x, mouse.y);
		}
		return true;
	}
	if (!editing || m_gizmoOperation != 3 || !engine.IsUIVisible()) return false;
	GameObject* object = scene.GetObject(m_selected < 0 ? SIZE_MAX : static_cast<size_t>(m_selected));
	RectTransform* rt = object != nullptr ? object->GetBehaviour<RectTransform>() : nullptr;
	UISystem::ElementGeometry g;
	if (rt == nullptr || object->GetParent() == nullptr || !ui.GetElementGeometry(object, g) || g.scale <= 0.0f) return false;
	if (m_rectDrag.active && m_rectDrag.objectId != object->GetId())
	{
		m_rectDrag.active = false;
	}

	// 캔버스 ↔ 씬 뷰 픽셀 (Overlay: y 위 + 캔버스, y 아래 + 픽셀)
	const float scale = g.scale;
	const float viewH = static_cast<float>(g.viewHeight);
	const auto toScreen = [&](const XMFLOAT2& c) { return ImVec2(x + c.x * scale, y + viewH - c.y * scale); };
	const auto toCanvas = [&](float px, float py) { return XMFLOAT2(px / scale, (viewH - py) / scale); };
	const auto local = [&](const XMFLOAT2& p) { return toScreen(g.toCanvas.Apply(p)); };
	const UIRect& r = g.rect;
	const XMFLOAT2 size(r.Width(), r.Height());
	const XMFLOAT2 pivotLocal(r.min.x + size.x * rt->pivot.x, r.min.y + size.y * rt->pivot.y);
	const float pw = g.parentRect.Width();
	const float ph = g.parentRect.Height();
	const XMFLOAT2 anchorPoints[4] = {
		XMFLOAT2(g.parentRect.min.x + rt->anchorMin.x * pw, g.parentRect.min.y + rt->anchorMin.y * ph),
		XMFLOAT2(g.parentRect.min.x + rt->anchorMax.x * pw, g.parentRect.min.y + rt->anchorMin.y * ph),
		XMFLOAT2(g.parentRect.min.x + rt->anchorMax.x * pw, g.parentRect.min.y + rt->anchorMax.y * ph),
		XMFLOAT2(g.parentRect.min.x + rt->anchorMin.x * pw, g.parentRect.min.y + rt->anchorMax.y * ph),
	};

	// ---- 그리기: 부모 사각형(점선 대신 흐린 선), 핸들 8 개, 피벗 원, 앵커 삼각형 4 개 ----
	ImDrawList* dl = ImGui::GetWindowDrawList();
	{
		const XMFLOAT2 pc[4] = { g.parentRect.min, XMFLOAT2(g.parentRect.max.x, g.parentRect.min.y), g.parentRect.max, XMFLOAT2(g.parentRect.min.x, g.parentRect.max.y) };
		ImVec2 pts[4];
		for (int i = 0; i < 4; ++i)
		{
			pts[i] = toScreen(g.parentToCanvas.Apply(pc[i]));
		}
		dl->AddPolyline(pts, 4, IM_COL32(255, 255, 255, 70), ImDrawFlags_Closed, 1.0f);
	}
	const ImU32 handleColor = IM_COL32(80, 150, 255, 255);
	ImVec2 handles[3][3];
	for (int hy = -1; hy <= 1; ++hy)
	{
		for (int hx = -1; hx <= 1; ++hx)
		{
			const XMFLOAT2 p(r.min.x + size.x * (hx + 1) * 0.5f, r.min.y + size.y * (hy + 1) * 0.5f);
			handles[hy + 1][hx + 1] = local(p);
			if (hx == 0 && hy == 0) continue;
			const ImVec2 h = handles[hy + 1][hx + 1];
			dl->AddRectFilled(ImVec2(h.x - 3.5f, h.y - 3.5f), ImVec2(h.x + 3.5f, h.y + 3.5f), IM_COL32(255, 255, 255, 255));
			dl->AddRect(ImVec2(h.x - 3.5f, h.y - 3.5f), ImVec2(h.x + 3.5f, h.y + 3.5f), handleColor);
		}
	}
	const ImVec2 pivotScreen = local(pivotLocal);
	dl->AddCircle(pivotScreen, 5.0f, handleColor, 16, 2.0f);
	ImVec2 anchorScreen[4];
	for (int i = 0; i < 4; ++i)
	{
		anchorScreen[i] = toScreen(g.parentToCanvas.Apply(anchorPoints[i]));
		// 삼각형 꼭짓점이 앵커, 바깥쪽(그 모서리 방향)으로 벌어짐
		const float sx = (i == 0 || i == 3) ? -1.0f : 1.0f;
		const float sy = (i == 0 || i == 1) ? 1.0f : -1.0f;   // 픽셀 y 아래 +
		const ImVec2 a = anchorScreen[i];
		dl->AddTriangleFilled(a, ImVec2(a.x + sx * 9.0f, a.y + sy * 4.0f), ImVec2(a.x + sx * 4.0f, a.y + sy * 9.0f), IM_COL32(255, 255, 255, 230));
		dl->AddTriangle(a, ImVec2(a.x + sx * 9.0f, a.y + sy * 4.0f), ImVec2(a.x + sx * 4.0f, a.y + sy * 9.0f), IM_COL32(40, 40, 40, 255));
	}
	if (g.driven[0] || g.driven[1] || g.drivenAnchors)
	{
		dl->AddText(ImVec2(handles[2][0].x, handles[2][0].y - 18.0f), IM_COL32(140, 190, 255, 255), "driven by layout");
	}

	const ImVec2 m(x + mouse.x, y + mouse.y);
	const auto isNear = [&](const ImVec2& p, float radius) { return std::fabs(p.x - m.x) <= radius && std::fabs(p.y - m.y) <= radius; };

	// ---- 누름: 피벗 → 앵커 → 핸들 → 안쪽 ----
	if (mouse.pressed && !m_rectDrag.active)
	{
		const bool inside = mouse.x >= 0.0f && mouse.y >= 0.0f && mouse.x < static_cast<float>(m_sceneWidth) && mouse.y < static_cast<float>(m_sceneHeight);
		if (!inside) return false;
		int mode = -1, hx = 0, hy = 0, anchor = 0;
		if (isNear(pivotScreen, 6.0f))
		{
			mode = 2;
		}
		for (int i = 0; i < 4 && mode < 0; ++i)
		{
			const float sx = (i == 0 || i == 3) ? -1.0f : 1.0f;
			const float sy = (i == 0 || i == 1) ? 1.0f : -1.0f;
			if (isNear(ImVec2(anchorScreen[i].x + sx * 5.0f, anchorScreen[i].y + sy * 5.0f), 5.0f))
			{
				mode = 3;
				anchor = i;
			}
		}
		for (int iy = -1; iy <= 1 && mode < 0; ++iy)
		{
			for (int ix = -1; ix <= 1 && mode < 0; ++ix)
			{
				if ((ix != 0 || iy != 0) && isNear(handles[iy + 1][ix + 1], 6.0f))
				{
					mode = 1;
					hx = ix;
					hy = iy;
				}
			}
		}
		if (mode < 0)
		{
			UIAffine inv;
			if (g.toCanvas.Invert(inv))
			{
				const XMFLOAT2 lm = inv.Apply(toCanvas(mouse.x, mouse.y));
				if (lm.x >= r.min.x && lm.x <= r.max.x && lm.y >= r.min.y && lm.y <= r.max.y)
				{
					mode = 0;
				}
			}
		}
		if (mode < 0) return false;
		m_rectDrag.active = true;
		m_rectDrag.mode = mode;
		m_rectDrag.hx = hx;
		m_rectDrag.hy = hy;
		m_rectDrag.anchor = anchor;
		m_rectDrag.objectId = object->GetId();
		m_rectDrag.moved = false;
		m_rectDrag.startX = mouse.x;
		m_rectDrag.startY = mouse.y;
		m_rectDrag.startAnchoredPosition = rt->anchoredPosition;
		m_rectDrag.startSizeDelta = rt->sizeDelta;
		m_rectDrag.startPivot = rt->pivot;
		m_rectDrag.startAnchorMin = rt->anchorMin;
		m_rectDrag.startAnchorMax = rt->anchorMax;
		m_rectStartGeometry = g;
		return true;
	}
	if (!m_rectDrag.active) return false;

	// ---- 드래그: 시작 상태 + 포인터 이동량 (매 프레임 시작 값에서 다시 계산 — 쌓이는 반올림 없음) ----
	if (std::fabs(mouse.x - m_rectDrag.startX) + std::fabs(mouse.y - m_rectDrag.startY) >= 2.0f)
	{
		m_rectDrag.moved = true;
	}
	if (!m_rectDrag.moved) return true;
	const UISystem::ElementGeometry& g0 = m_rectStartGeometry;
	const XMFLOAT2 c0 = toCanvas(m_rectDrag.startX, m_rectDrag.startY);
	const XMFLOAT2 c1 = toCanvas(mouse.x, mouse.y);
	const UIRect& r0 = g0.rect;
	const XMFLOAT2 size0(r0.Width(), r0.Height());
	const XMFLOAT2 pivot0 = m_rectDrag.startPivot;
	const XMFLOAT2 pivotPoint0(r0.min.x + size0.x * pivot0.x, r0.min.y + size0.y * pivot0.y);
	const UIAffine rs = LocalLinear(*object);
	const bool rotated = std::fabs(rs.b) > 1e-5f || std::fabs(rs.c) > 1e-5f;
	UIAffine invParent, invLocal;
	if (!g0.parentToCanvas.Invert(invParent) || !g0.toCanvas.Invert(invLocal)) return true;
	const float parentScale = std::sqrt(std::fabs(g0.parentToCanvas.a * g0.parentToCanvas.d - g0.parentToCanvas.b * g0.parentToCanvas.c));
	const float snapDistance = mouse.ctrl ? 0.0f : 8.0f / (scale * (std::max)(parentScale, 1e-4f));   // 8 출력 픽셀 → 부모 단위

	// 스냅 대상: 부모 사각형의 가장자리·가운데, 형제 사각형의 가장자리·가운데 (부모 공간)
	std::vector<float> targets[2];
	const auto addTargets = [&](const UIRect& t)
	{
		targets[0].insert(targets[0].end(), { t.min.x, (t.min.x + t.max.x) * 0.5f, t.max.x });
		targets[1].insert(targets[1].end(), { t.min.y, (t.min.y + t.max.y) * 0.5f, t.max.y });
	};
	addTargets(g0.parentRect);
	for (const GameObject* sibling : object->GetParent()->GetChildren())
	{
		UISystem::ElementGeometry sg;
		if (sibling != object && ui.GetElementGeometry(sibling, sg))
		{
			addTargets(sg.rect);
		}
	}
	// values 중 대상에 가장 가까운 것까지의 차 (snapDistance 안에서). 없으면 0
	const auto snapDelta = [&](int axis, std::initializer_list<float> values)
	{
		float best = 0.0f;
		float bestDistance = snapDistance;
		for (float v : values)
		{
			for (float t : targets[axis])
			{
				const float d = std::fabs(t - v);
				if (d < bestDistance)
				{
					bestDistance = d;
					best = t - v;
				}
			}
		}
		return best;
	};

	if (m_rectDrag.mode == 0)
	{
		// 이동: 부모 공간의 포인터 이동량
		XMFLOAT2 d = LinearApply(invParent, XMFLOAT2(c1.x - c0.x, c1.y - c0.y));
		if (snapDistance > 0.0f && !rotated)
		{
			d.x += snapDelta(0, { r0.min.x + d.x, (r0.min.x + r0.max.x) * 0.5f + d.x, r0.max.x + d.x });
			d.y += snapDelta(1, { r0.min.y + d.y, (r0.min.y + r0.max.y) * 0.5f + d.y, r0.max.y + d.y });
		}
		rt->anchoredPosition = XMFLOAT2(m_rectDrag.startAnchoredPosition.x + d.x, m_rectDrag.startAnchoredPosition.y + d.y);
	}
	else if (m_rectDrag.mode == 1)
	{
		// 크기: 요소 자신의 축(회전 전 사각형 공간)에서 핸들 쪽 변을 옮김. Alt = 반대쪽도 같이 (가운데 고정), Shift = 비율 유지
		const XMFLOAT2 l0 = invLocal.Apply(c0);
		const XMFLOAT2 l1 = invLocal.Apply(c1);
		XMFLOAT2 d(l1.x - l0.x, l1.y - l0.y);
		const int hx = m_rectDrag.hx;
		const int hy = m_rectDrag.hy;
		if (snapDistance > 0.0f && !rotated)
		{
			if (hx != 0) d.x += snapDelta(0, { (hx < 0 ? r0.min.x : r0.max.x) + d.x });
			if (hy != 0) d.y += snapDelta(1, { (hy < 0 ? r0.min.y : r0.max.y) + d.y });
		}
		float w = size0.x + static_cast<float>(hx) * d.x * (mouse.alt ? 2.0f : 1.0f);
		float h = size0.y + static_cast<float>(hy) * d.y * (mouse.alt ? 2.0f : 1.0f);
		if (mouse.shift && hx != 0 && hy != 0 && size0.x > 0.0f && size0.y > 0.0f)
		{
			const float sx = w / size0.x;
			const float sy = h / size0.y;
			const float s = std::fabs(sx - 1.0f) >= std::fabs(sy - 1.0f) ? sx : sy;
			w = size0.x * s;
			h = size0.y * s;
		}
		w = (std::max)(w, 0.0f);
		h = (std::max)(h, 0.0f);
		UIRect nr;
		const auto place = [&](int handle, float lo, float hi, float newSize, float& outMin)
		{
			if (mouse.alt || handle == 0) outMin = (lo + hi) * 0.5f - newSize * 0.5f;   // 가운데 고정 (핸들이 없는 축도 가운데 기준)
			else if (handle > 0) outMin = lo;                                          // 오른쪽·위 핸들: 왼쪽·아래 고정
			else outMin = hi - newSize;
		};
		if (hx == 0) w = size0.x;
		if (hy == 0) h = size0.y;
		place(hx, r0.min.x, r0.max.x, w, nr.min.x);
		place(hy, r0.min.y, r0.max.y, h, nr.min.y);
		if (hx == 0) nr.min.x = r0.min.x;
		if (hy == 0) nr.min.y = r0.min.y;
		nr.max = XMFLOAT2(nr.min.x + w, nr.min.y + h);
		ApplyRectValues(*rt, g0.parentRect, nr, m_rectDrag.startAnchorMin, m_rectDrag.startAnchorMax, pivot0);
		// 회전한 요소: 피벗(회전 중심)이 옮겨간 만큼 되돌려 고정한 변이 화면에서 그대로 있게
		const XMFLOAT2 pivotPoint1(nr.min.x + w * pivot0.x, nr.min.y + h * pivot0.y);
		const XMFLOAT2 t = PivotCorrection(rs, pivotPoint0, pivotPoint1);
		rt->anchoredPosition.x += t.x;
		rt->anchoredPosition.y += t.y;
	}
	else if (m_rectDrag.mode == 2)
	{
		// 피벗: 사각형은 그대로, 회전 중심만 옮김 (0·0.5·1 근처에서 붙음)
		const XMFLOAT2 l1 = invLocal.Apply(c1);
		XMFLOAT2 pivot(size0.x > 0.0f ? (l1.x - r0.min.x) / size0.x : pivot0.x, size0.y > 0.0f ? (l1.y - r0.min.y) / size0.y : pivot0.y);
		if (!mouse.ctrl)
		{
			pivot.x = SnapUnit(pivot.x, 0.05f);
			pivot.y = SnapUnit(pivot.y, 0.05f);
		}
		ApplyRectValues(*rt, g0.parentRect, r0, m_rectDrag.startAnchorMin, m_rectDrag.startAnchorMax, pivot);
		const XMFLOAT2 pivotPoint1(r0.min.x + size0.x * pivot.x, r0.min.y + size0.y * pivot.y);
		const XMFLOAT2 t = PivotCorrection(rs, pivotPoint0, pivotPoint1);
		rt->anchoredPosition.x += t.x;
		rt->anchoredPosition.y += t.y;
	}
	else
	{
		// 앵커: 부모 사각형 안의 비율 (0~1, 0·0.5·1 근처에서 붙음). 사각형은 그대로
		const XMFLOAT2 p1 = invParent.Apply(c1);
		XMFLOAT2 a(pw > 0.0f ? (p1.x - g0.parentRect.min.x) / pw : 0.0f, ph > 0.0f ? (p1.y - g0.parentRect.min.y) / ph : 0.0f);
		a.x = (std::clamp)(mouse.ctrl ? a.x : SnapUnit(a.x, 0.03f), 0.0f, 1.0f);
		a.y = (std::clamp)(mouse.ctrl ? a.y : SnapUnit(a.y, 0.03f), 0.0f, 1.0f);
		XMFLOAT2 amin = m_rectDrag.startAnchorMin;
		XMFLOAT2 amax = m_rectDrag.startAnchorMax;
		const int i = m_rectDrag.anchor;
		if (i == 0 || i == 3) amin.x = (std::min)(a.x, amax.x); else amax.x = (std::max)(a.x, amin.x);
		if (i == 0 || i == 1) amin.y = (std::min)(a.y, amax.y); else amax.y = (std::max)(a.y, amin.y);
		ApplyRectValues(*rt, g0.parentRect, r0, amin, amax, pivot0);
	}
	return true;
}

void Editor::NudgeSelectedUI(Engine& engine)
{
	Scene& scene = engine.GetScene();
	GameObject* object = scene.GetObject(m_selected < 0 ? SIZE_MAX : static_cast<size_t>(m_selected));
	RectTransform* rt = object != nullptr && object->GetParent() != nullptr && UISystem::IsUnderScreenCanvas(*object) ? object->GetBehaviour<RectTransform>() : nullptr;
	if (rt == nullptr) return;
	const float step = ImGui::GetIO().KeyShift ? 10.0f : 1.0f;
	if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) rt->anchoredPosition.x -= step;
	if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) rt->anchoredPosition.x += step;
	if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) rt->anchoredPosition.y += step;
	if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) rt->anchoredPosition.y -= step;
}

void Editor::DrawWorldCanvasOutlines(Engine& engine)
{
	// World 캔버스 루트의 사각형 (sizeDelta·피벗, 루트 월드 행렬). 편집 중 위치를 잡을 때 보이게
	DebugDraw& debug = engine.GetDebugDraw();
	for (const auto& owned : engine.GetScene().GetObjects())
	{
		const GameObject& object = *owned;
		const GameObject* canvasObject = nullptr;
		const Canvas* canvas = UISystem::FindCanvas(object, &canvasObject);
		if (canvas == nullptr || canvasObject != &object || canvas->renderMode != Canvas::World) continue;
		const RectTransform* rt = object.GetBehaviour<RectTransform>();
		const XMFLOAT2 size = rt != nullptr ? rt->sizeDelta : XMFLOAT2(100.0f, 100.0f);
		const XMFLOAT2 pivot = rt != nullptr ? rt->pivot : XMFLOAT2(0.5f, 0.5f);
		const XMMATRIX world = object.GetWorldMatrix();
		const float x0 = -size.x * pivot.x, y0 = -size.y * pivot.y;
		const XMFLOAT2 corners[4] = { XMFLOAT2(x0, y0), XMFLOAT2(x0 + size.x, y0), XMFLOAT2(x0 + size.x, y0 + size.y), XMFLOAT2(x0, y0 + size.y) };
		XMFLOAT3 p[4];
		for (int i = 0; i < 4; ++i)
		{
			XMStoreFloat3(&p[i], XMVector3TransformCoord(XMVectorSet(corners[i].x, corners[i].y, 0.0f, 1.0f), world));
		}
		for (int i = 0; i < 4; ++i)
		{
			debug.Line(p[i], p[(i + 1) % 4], 0xFFE0E0E0u);
		}
	}
}

void Editor::DrawCameraGizmos(Engine& engine, float x, float y, float width, float height)
{
	if (playState != PlayState::Editing || width <= 0.0f || height <= 0.0f) return;
	Scene& scene = engine.GetScene();
	const Camera& editorCamera = engine.GetCamera();
	const XMMATRIX viewProj = editorCamera.GetViewMatrix() * editorCamera.GetProjectionMatrix();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);   // 이미지 밖(툴바)으로 선이 새지 않게
	const auto project = [&](const XMFLOAT3& p, ImVec2& out) -> bool
	{
		return ProjectToView(viewProj, x, y, width, height, p, out);
	};
	const auto line = [&](const XMFLOAT3& a, const XMFLOAT3& b, ImU32 color, float thickness)
	{
		ImVec2 pa, pb;
		if (project(a, pa) && project(b, pb))
		{
			dl->AddLine(pa, pb, color, thickness);
		}
	};

	auto& objects = scene.GetObjects();
	for (int i = 0; i < static_cast<int>(objects.size()); ++i)
	{
		CameraComponent* cameraComponent = objects[i]->GetBehaviour<CameraComponent>();
		if (cameraComponent == nullptr) continue;
		// 에디터 눈과 겹치는 카메라(방금 "Align to view" 한 것)는 프러스텀이 화면 테두리와 일치해 의미가 없음 — 건너뜀.
		const XMFLOAT3 eyeDelta(cameraComponent->GetPose().position.x - editorCamera.GetPosition().x, cameraComponent->GetPose().position.y - editorCamera.GetPosition().y, cameraComponent->GetPose().position.z - editorCamera.GetPosition().z);
		if (eyeDelta.x * eyeDelta.x + eyeDelta.y * eyeDelta.y + eyeDelta.z * eyeDelta.z < 0.01f) continue;
		const CameraPose pose = cameraComponent->GetPose();
		const XMMATRIX rotation = pose.GetRotationMatrix();   // 월드 회전 (부모 roll 포함) 그대로
		const XMVECTOR eye = XMLoadFloat3(&pose.position);
		const XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotation);
		const XMVECTOR right = XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rotation);
		const XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), rotation);

		// 눈에서 1.5 단위 앞의 프러스텀 단면 (fov 는 컴포넌트 값, 종횡비는 씬 뷰 값). 위쪽의 작은 삼각형 = "위".
		const float depth = 1.5f;
		const bool ortho = cameraComponent->orthographic;   // 직교 카메라는 피라미드가 아니라 상자 (눈 평면의 사각형 → 앞의 사각형)
		const float halfH = ortho ? cameraComponent->orthoSize * 0.5f : depth * tanf(pose.fovY * 0.5f);
		const float halfW = halfH * editorCamera.GetAspect();
		const XMVECTOR center = eye + forward * depth;
		XMFLOAT3 corner[4], apex, top;
		XMStoreFloat3(&corner[0], center - right * halfW + up * halfH);
		XMStoreFloat3(&corner[1], center + right * halfW + up * halfH);
		XMStoreFloat3(&corner[2], center + right * halfW - up * halfH);
		XMStoreFloat3(&corner[3], center - right * halfW - up * halfH);
		XMStoreFloat3(&apex, eye);
		XMFLOAT3 nearCorner[4];
		XMStoreFloat3(&nearCorner[0], eye - right * halfW + up * halfH);
		XMStoreFloat3(&nearCorner[1], eye + right * halfW + up * halfH);
		XMStoreFloat3(&nearCorner[2], eye + right * halfW - up * halfH);
		XMStoreFloat3(&nearCorner[3], eye - right * halfW - up * halfH);
		XMStoreFloat3(&top, center + up * (halfH * 1.5f));

		const bool selected = i == m_selected;
		const ImU32 color = selected ? IM_COL32(255, 200, 40, 255) : IM_COL32(120, 190, 255, 220);
		const float thickness = selected ? 2.0f : 1.2f;
		for (int c = 0; c < 4; ++c)
		{
			if (ortho)
			{
				line(nearCorner[c], corner[c], color, thickness);
				line(nearCorner[c], nearCorner[(c + 1) % 4], color, thickness);
			}
			else
			{
				line(apex, corner[c], color, thickness);
			}
			line(corner[c], corner[(c + 1) % 4], color, thickness);
		}
		line(corner[0], top, color, thickness);
		line(corner[1], top, color, thickness);

		ImVec2 label;
		if (project(pose.position, label))
		{
			dl->AddCircleFilled(label, 4.0f, color);
			dl->AddText(ImVec2(label.x + 8.0f, label.y - 8.0f), color, objects[i]->GetName().c_str());
		}
	}
	dl->PopClipRect();
}

void Editor::DrawCameraPreview(Engine& engine, float x, float y, float width, float height)
{
	Renderer& renderer = engine.GetRenderer();
	Scene& scene = engine.GetScene();
	CameraComponent* cameraComponent = (m_selected >= 0 && m_selected < static_cast<int>(scene.GetObjects().size()))
		? scene.GetObjects()[m_selected]->GetBehaviour<CameraComponent>()
		: nullptr;
	// 재생 중 활성 카메라를 선택했으면 씬 뷰가 이미 그 시점임.
	const bool wanted = cameraComponent != nullptr && !(playState != PlayState::Editing && scene.GetActiveCamera() == cameraComponent) && width >= 240.0f && height >= 160.0f;
	if (!wanted)
	{
		renderer.SetPreviewCamera(nullptr);
		renderer.SetPreviewTarget(0, 0);   // 텍스처도 반납함
		return;
	}

	// 폭은 씬 뷰의 32%, 비율은 씬 뷰와 같게 함 (카메라 종횡비가 화면을 따르므로).
	const float previewWidth = floorf(width * 0.32f);
	const float previewHeight = floorf(previewWidth * height / width);
	renderer.SetPreviewTarget(static_cast<uint32_t>(previewWidth), static_cast<uint32_t>(previewHeight));
	cameraComponent->ApplyTo(m_previewCamera, true);
	m_previewCamera.SetAspectRatio(previewWidth / previewHeight);
	renderer.SetPreviewCamera(&m_previewCamera, scene.GetObjects()[m_selected]->GetId());   // 이번 프레임의 Render 가 이 카메라로 미리보기 패스를 그림. id = 대상이 바뀌면 히스토리 리셋

	const TextureHandle texture = renderer.GetPreviewTexture();
	if (!texture.IsValid()) return;
	const uint64_t textureId = engine.GetDevice().GetImGuiTextureId(texture);
	if (textureId == 0) return;

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float margin = 12.0f;
	const ImVec2 rectMin(x + width - previewWidth - margin, y + height - previewHeight - margin);   // min/max 는 windows.h 매크로
	const ImVec2 rectMax(rectMin.x + previewWidth, rectMin.y + previewHeight);
	const ImU32 frame = IM_COL32(255, 200, 40, 255);
	dl->AddRectFilled(ImVec2(rectMin.x - 2.0f, rectMin.y - 2.0f), ImVec2(rectMax.x + 2.0f, rectMax.y + 2.0f), IM_COL32(20, 22, 40, 230), 3.0f);
	dl->AddImage(static_cast<ImTextureID>(textureId), rectMin, rectMax);
	dl->AddRect(rectMin, rectMax, frame, 0.0f, 0, 1.5f);
	const std::string label = scene.GetObjects()[m_selected]->GetName() + "  (camera preview)";
	const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
	dl->AddRectFilled(ImVec2(rectMin.x, rectMin.y - labelSize.y - 6.0f), ImVec2(rectMin.x + labelSize.x + 10.0f, rectMin.y - 2.0f), IM_COL32(20, 22, 40, 230), 3.0f);
	dl->AddText(ImVec2(rectMin.x + 5.0f, rectMin.y - labelSize.y - 4.0f), frame, label.c_str());
}

void Editor::OnProjectChanged()
{
	m_contentBrowser.Refresh();
	m_contentBrowser.SetDirectory(L"", 0);
	m_selected = -1;
	m_buildScenes.clear();
	m_buildOptions.startScene.clear();
	m_buildOptions.name = project.name.empty() ? "MyGame" : project.name;
	scenePath = Paths::GetSceneDir() + L"scene.json";
	m_pathBuffer[0] = '\0';
}

void Editor::DrawProjectBanner(const Callbacks& callbacks)
{
	// 스크립트는 exe 가 아니라 <이름>Scripts.dll 에 있음. 배너는 "스크립트가 있는데 DLL 이 안 올라옴" 과
	// "새 빌드가 있지만 재생 중이라 아직 못 올림" 두 경우에만 뜸. 스크립트가 없는 프로젝트(렌더링 실험 등)는
	// 배너가 없음.
	if (!project.loaded) return;
	const bool missing = project.scriptSources > 0 && !project.scriptsLoaded;
	if (!missing && !project.scriptsPendingReload) return;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.45f, 0.30f, 0.05f, 0.92f));
	ImGui::BeginChild("##projectBanner", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize);
	if (project.scriptsPendingReload)
	{
		ImGui::TextWrapped("A new build of %sScripts.dll was found. It will be loaded when play stops.", project.name.c_str());
	}
	else
	{
		ImGui::TextWrapped("Scripts of project '%s' are not loaded (%s). Objects using them keep working with the other components; build the scripts to use them.", project.name.c_str(), project.scriptsError.empty() ? "not built" : project.scriptsError.c_str());
		if (project.hasSolution)
		{
			if (ImGui::Button("Build scripts") && callbacks.buildScripts)
			{
				callbacks.buildScripts();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("MSBuild %sScripts (%s|x64) in %s, then load the DLL without restarting", project.name.c_str(),
#if defined(_DEBUG)
								  "Debug",
#else
								  "Release",
#endif
								  Log::ToUtf8(project.solutionPath.c_str()).c_str());
			}
			ImGui::SameLine();
		}
		ImGui::TextDisabled("or build %sScripts in Visual Studio — the editor reloads it automatically", project.name.c_str());
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void Editor::DrawLauncher(Engine& engine, const Callbacks& callbacks)
{
	(void)engine;
	ImGui::SetNextWindowSize(ImVec2(640.0f, 0.0f), ImGuiCond_Appearing);
	if (!ImGui::BeginPopupModal("Projects", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	if (m_newProjectParent.empty())
	{
		m_newProjectParent = ProjectLauncher::GetDefaultProjectsDir();
	}

	ImGui::SeparatorText("Recent");
	const std::vector<ProjectLauncher::Recent> recent = ProjectLauncher::LoadRecent();
	if (recent.empty())
	{
		ImGui::TextDisabled("(no recent projects)");
	}
	for (size_t i = 0; i < recent.size(); ++i)
	{
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::Button("Open"))
		{
			if (callbacks.openProject && callbacks.openProject(recent[i].path))
			{
				ImGui::CloseCurrentPopup();
			}
			else
			{
				m_launcherMessage = "open failed";
			}
		}
		ImGui::SameLine();
		ImGui::Text("%s", recent[i].name.c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("%s", Log::ToUtf8(recent[i].path.c_str()).c_str());
		ImGui::PopID();
	}

	ImGui::SeparatorText("Open");
	if (ImGui::Button("Browse for a .sherlock file..."))
	{
		const std::wstring path = ProjectLauncher::BrowseForProjectFile(nullptr);
		if (!path.empty())
		{
			if (callbacks.openProject && callbacks.openProject(path))
			{
				ImGui::CloseCurrentPopup();
			}
			else
			{
				m_launcherMessage = "open failed";
			}
		}
	}

	ImGui::SeparatorText("New");
	ImGui::SetNextItemWidth(220.0f);
	ImGui::InputText("Name", m_newProjectName, sizeof(m_newProjectName), ImGuiInputTextFlags_CharsNoBlank);
	ImGui::Text("Location: %s", Log::ToUtf8(m_newProjectParent.c_str()).c_str());
	ImGui::SameLine();
	if (ImGui::SmallButton("Browse..."))
	{
		const std::wstring dir = ProjectLauncher::BrowseForFolder(nullptr, m_newProjectParent);
		if (!dir.empty())
		{
			m_newProjectParent = dir + L"\\";
		}
	}
	ImGui::TextDisabled("Creates <Location>\\<Name>\\ with Assets, Scripts, a Main.json scene and <Name>.sln (editor + game targets).");
	ImGui::BeginDisabled(!Project::IsValidName(m_newProjectName));
	if (ImGui::Button("Create and open") && callbacks.newProject)
	{
		std::string error;
		if (callbacks.newProject(m_newProjectParent, m_newProjectName, error))
		{
			m_launcherMessage.clear();
			ImGui::CloseCurrentPopup();
		}
		else
		{
			m_launcherMessage = error;
		}
	}
	ImGui::EndDisabled();
	if (!m_launcherMessage.empty())
	{
		ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "%s", m_launcherMessage.c_str());
	}
	ImGui::Separator();
	if (ImGui::Button("Close"))
	{
		m_launcherMessage.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Editor::DrawProjectSettingsPopup(const Callbacks& callbacks)
{
	if (!ImGui::BeginPopupModal("Project Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	ImGui::Text("%s", project.name.c_str());
	ImGui::TextDisabled("%s", Log::ToUtf8(project.root.c_str()).c_str());
	if (m_buildScenes.empty())
	{
		m_buildScenes = GameBuilder::ListScenes();
	}
	if (projectStartScene != nullptr)
	{
		ImGui::SetNextItemWidth(260.0f);
		if (ImGui::BeginCombo("Start scene", projectStartScene->c_str()))
		{
			for (const std::string& scene : m_buildScenes)
			{
				if (ImGui::Selectable(scene.c_str(), scene == *projectStartScene))
				{
					*projectStartScene = scene;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("refresh"))
		{
			m_buildScenes = GameBuilder::ListScenes();
		}
	}
	ImGui::TextDisabled("Solution: %s", project.hasSolution ? Log::ToUtf8(project.solutionPath.c_str()).c_str() : "(none)");
	// 스크립트 DLL 상태와 빌드 버튼
	if (project.scriptsLoaded)
	{
		ImGui::TextDisabled("Scripts DLL: loaded, %zu component(s), loaded %u time(s)", project.scriptTypes, project.scriptReloads);
	}
	else
	{
		ImGui::TextDisabled("Scripts DLL: not loaded (%s)", project.scriptsError.empty() ? "no scripts" : project.scriptsError.c_str());
	}
	ImGui::TextDisabled("  %s", Log::ToUtf8(project.scriptsDll.c_str()).c_str());
	if (project.hasSolution && callbacks.buildScripts)
	{
		if (ImGui::Button("Build scripts"))
		{
			callbacks.buildScripts();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%zu source file(s); a newer build is also picked up automatically while editing)", project.scriptSources);
	}
	ImGui::Separator();
	if (ImGui::Button("Save") && callbacks.saveProject)
	{
		m_lastMessage = callbacks.saveProject() ? "project saved" : "project save failed";
	}
	ImGui::SameLine();
	if (ImGui::Button("Close"))
	{
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Editor::DrawBuildPopup()
{
	// 언리얼의 Package Project 에 해당. File 메뉴 항목은 플래그만 세우고, 메뉴를 닫은 뒤(EndMenu 뒤)
	// OpenPopup 해서 여기서 그림.
	if (!ImGui::BeginPopupModal("Build Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	if (!GameBuilder::IsAvailable())
	{
		ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "no project is open (File > Projects)");
		if (ImGui::Button("Close"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
		return;
	}
	m_buildOptions.projectName = project.name;
	m_buildOptions.projectSolution = project.solutionPath;
	if (project.scriptSources > 0 && !project.scriptsLoaded)
	{
		ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "warning: %sScripts.dll is not built; the package will not have the scripts (build them first)", project.name.c_str());
	}
	if (m_buildScenes.empty())
	{
		m_buildScenes = GameBuilder::ListScenes();
	}
	if (m_buildOptions.startScene.empty() && !m_buildScenes.empty())
	{
		// 기본: 지금 열린 씬 파일이 목록에 있으면 그것, 아니면 첫 항목
		const std::string current = FileStem(scenePath) + ".json";
		m_buildOptions.startScene = std::find(m_buildScenes.begin(), m_buildScenes.end(), current) != m_buildScenes.end() ? current : m_buildScenes.front();
	}
	char name[64];
	strncpy_s(name, m_buildOptions.name.c_str(), sizeof(name) - 1);
	ImGui::SetNextItemWidth(260.0f);
	if (ImGui::InputText("Game name", name, sizeof(name)))
	{
		m_buildOptions.name = name;
	}
	ImGui::SetNextItemWidth(260.0f);
	if (ImGui::BeginCombo("Start scene", m_buildOptions.startScene.c_str()))
	{
		for (const std::string& scene : m_buildScenes)
		{
			if (ImGui::Selectable(scene.c_str(), scene == m_buildOptions.startScene))
			{
				m_buildOptions.startScene = scene;
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::SmallButton("refresh scenes"))
	{
		m_buildScenes = GameBuilder::ListScenes();
	}
	ImGui::TextDisabled("(save the scene first: Ctrl+S writes Assets\\Scenes)");
	ImGui::Checkbox("Copy all assets", &m_buildOptions.copyAllAssets);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("off: only Config, Textures and the models the start scene references (skips Sponza etc.)");
	}
	ImGui::Checkbox("Build Release with MSBuild first", &m_buildOptions.releaseBuild);
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("runs MSBuild on the project's game target (Release|x64); the editor freezes until it finishes. Off: uses the runtime next to this editor");
	}
	ImGui::Checkbox("Open folder when done", &m_buildOptions.openFolder);
	ImGui::TextDisabled("%s", Log::ToUtf8((GameBuilder::GetBuildRoot() + std::wstring(m_buildOptions.name.begin(), m_buildOptions.name.end()) + L"\\").c_str()).c_str());
	if (!m_buildMessage.empty())
	{
		ImGui::TextWrapped("%s", m_buildMessage.c_str());
	}
	ImGui::Separator();
	if (ImGui::Button("Build"))
	{
		const GameBuilder::Result result = GameBuilder::Build(m_buildOptions);
		m_buildMessage = result.message;
		m_lastMessage = result.message;
		if (result.ok)
		{
			m_buildMessage += "\n" + Log::ToUtf8(result.outputDir.c_str());
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Close"))
	{
		m_buildMessage.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Editor::DrawPlayControls(const Callbacks& callbacks)
{
	const bool editing = playState == PlayState::Editing;
	const bool playing = playState == PlayState::Playing;
	if (editing)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.6f, 0.3f, 1.0f));
		if (ImGui::Button("Play") && callbacks.play)
		{
			callbacks.play();
		}
		ImGui::PopStyleColor();
	}
	else
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.25f, 0.25f, 1.0f));
		if (ImGui::Button("Stop") && callbacks.stop)
		{
			callbacks.stop();
		}
		ImGui::PopStyleColor();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(editing);
	if (ImGui::Button(playing ? "Pause" : "Resume") && callbacks.pauseToggle)
	{
		callbacks.pauseToggle();
	}
	ImGui::SameLine();
	if (ImGui::Button("Step") && callbacks.stepFrame)
	{
		callbacks.stepFrame();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	DebugUI::SliderFloatR("##timeScale", &timeScale, 1.0f, 0.0f, 4.0f, "x%.2f");
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("time scale (dt multiplier while playing)");
	}
}

void Editor::DrawComponents(Engine& engine, GameObject& object)
{
	auto& behaviours = object.GetBehaviours();
	ImGui::Text("Components (%zu)", behaviours.size());
	int removeIndex = -1;
	for (int i = 0; i < static_cast<int>(behaviours.size()); ++i)
	{
		Behaviour& behaviour = *behaviours[i];
		ImGui::PushID(i);
		if (ImGui::SmallButton("x"))
		{
			removeIndex = i;
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("remove component");
		}
		ImGui::SameLine();
		ImGui::Checkbox("##enabled", &behaviour.enabled);
		ImGui::SameLine();
		const bool isScript = BehaviourRegistry::IsScript(behaviour.GetTypeName());
		if (isScript)
		{
			// 스크립트 소스 열기 (<프로젝트>\Scripts\<이름>.cpp). 고친 뒤 빌드·재실행.
			if (ImGui::SmallButton("Edit"))
			{
				m_lastMessage = ScriptCreator::OpenScript(behaviour.GetTypeName()) ? std::string("opened ") + behaviour.GetTypeName() + ".h/.cpp (Build scripts to reload)" : "script source not found in the project Scripts folder";
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("open Scripts/%s.h and .cpp (after editing: Build scripts, or build in Visual Studio — the editor reloads the DLL)", behaviour.GetTypeName());
			}
			ImGui::SameLine();
		}
		const bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed, "%s%s", behaviour.GetTypeName(), isScript ? "  (Script)" : "");
		if (open)
		{
			if (RectTransform* rt = dynamic_cast<RectTransform*>(&behaviour))
			{
				DrawAnchorPresets(*rt);
			}
			UIObjectRef::SetSceneContext(&engine.GetScene());
			ImGuiPropertyVisitor visitor(&object, &engine.GetScene());
			if (dynamic_cast<RectTransform*>(&behaviour) != nullptr)
			{
				DrawDrivenNote(engine.GetUI(), object);
			}
			behaviour.Reflect(visitor);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (removeIndex >= 0)
	{
		object.RemoveBehaviour(static_cast<size_t>(removeIndex));
	}

	// 스크립트 DLL 이 없어 만들지 못한 컴포넌트. 값은 보관 중이며 Build scripts 뒤 핫리로드가 되살림
	auto& missing = object.GetMissingComponents();
	int removeMissing = -1;
	for (int i = 0; i < static_cast<int>(missing.size()); ++i)
	{
		const size_t typeAt = missing[i].find("\"type\":\"");
		std::string type = "?";
		if (typeAt != std::string::npos)
		{
			const size_t begin = typeAt + 8;
			type = missing[i].substr(begin, missing[i].find('"', begin) - begin);
		}
		ImGui::PushID(1000 + i);
		if (ImGui::SmallButton("x"))
		{
			removeMissing = i;
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("remove (the saved values are lost)");
		}
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s  (Missing script)", type.c_str());
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("not registered: build the project scripts. Values are kept and restored on reload.");
		}
		ImGui::PopID();
	}
	if (removeMissing >= 0)
	{
		missing.erase(missing.begin() + removeMissing);
	}

	// 추가: 레지스트리에 등록된 C++ 클래스 목록. 스크립트(프로젝트의 Scripts, SHERLOCK_SCRIPT)와 엔진
	// 컴포넌트를 나눠 보여 줌.
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::BeginCombo("##addComponent", "Add Component..."))
	{
		for (int group = 0; group < 2; ++group)
		{
			const bool scripts = group == 0;
			ImGui::SeparatorText(scripts ? "Scripts (project DLL)" : "Components");
			for (const std::string& name : BehaviourRegistry::GetTypeNames())
			{
				if (BehaviourRegistry::IsScript(name) != scripts) continue;
				if (ImGui::Selectable(name.c_str()))
				{
					object.AddBehaviour(name);
					m_lastMessage = "added " + name + " to " + object.GetName();
				}
			}
		}
		ImGui::EndCombo();
	}
	if (BehaviourRegistry::GetTypeNames().empty())
	{
		ImGui::TextDisabled("(nothing registered — add a class in Scripts with SHERLOCK_SCRIPT)");
	}

	// 새 스크립트 (유니티의 Create > C# Script 에 해당). 파일 생성과 편집기로 열기까지 함. Build scripts 하면
	// 에디터가 DLL 을 다시 올려 목록에 나타남 (재실행 불필요).
	if (ImGui::Button("New Script..."))
	{
		ImGui::OpenPopup("New Script");
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("create Scripts/<Name>.h + .cpp from a template and open them");
	}
	if (ImGui::BeginPopupModal("New Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (!ScriptCreator::IsAvailable())
		{
			ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "source tree not found - run from the repository build (x64\\Debug)");
			if (ImGui::Button("Close"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		else
		{
			ImGui::TextUnformatted("Class name (C++ identifier, e.g. EnemyPatrol):");
			ImGui::SetNextItemWidth(320.0f);
			const bool enter = ImGui::InputText("##name", m_newScriptName, sizeof(m_newScriptName), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank);
			ImGui::TextDisabled("%s", Log::ToUtf8((ScriptCreator::GetScriptsDir() + L"<Name>.h  +  <Name>.cpp").c_str()).c_str());
			ImGui::TextDisabled("Both files open in your editor. Build scripts (or build in Visual Studio) - the editor reloads the DLL.");
			if (!m_newScriptError.empty())
			{
				ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "%s", m_newScriptError.c_str());
			}
			const bool valid = ScriptCreator::IsValidClassName(m_newScriptName);
			ImGui::BeginDisabled(!valid);
			if (ImGui::Button("Create and open") || (enter && valid))
			{
				std::string error;
				if (ScriptCreator::Create(m_newScriptName, error))
				{
					ScriptCreator::OpenScript(m_newScriptName);
					m_lastMessage = std::string("created ") + m_newScriptName + ".cpp - Build scripts to use it";
					m_newScriptError.clear();
					m_newScriptName[0] = '\0';
					ImGui::CloseCurrentPopup();
				}
				else
				{
					m_newScriptError = error;
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				m_newScriptError.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
}

void Editor::DrawRenderSettings(Engine& engine)
{
	DebugUI::BeginPanel("Render Settings");
	DebugUI::DrawRenderSettingsPanel(engine.GetRenderer(), engine.GetDevice());
	RenderSettings& settings = engine.GetRenderer().GetSettings();
	// 11~15: 디퍼드·클러스터드·SSAO 검증용 (docs/plan/deferred-clustered-ssao-record.html). 13 (SSAO) 은 디퍼드 프레임에서만,
	// 5 (UV) 는 G-buffer 에 UV 가 없어 그 프레임을 포워드로 그림.
	const char* views[] = { "Lit", "Albedo", "World normal", "Depth", "Shadow factor", "UV", "Metal(B)/Rough(G)", "Occlusion", "Shadow cascade", "Specular F0", "Clearcoat",
							"World position (frac)", "Geometric normal", "SSAO", "Cluster light count", "Cluster depth slice",
							"Velocity", "Velocity error", "Rain exposure/wet/puddle" };
	ImGui::Combo("Debug view", &settings.debugView, views, IM_ARRAYSIZE(views));
	if (settings.debugView == 3)
	{
		DebugUI::SliderFloatR("Depth range", &settings.debugDepthRange, RenderSettings().debugDepthRange, 1.0f, 500.0f);
	}
	const char* boneModes[] = { "Off", "Bones + joints", "Bones + joints + axes" };
	ImGui::Combo("Bone debug (B)", &settings.debugBones, boneModes, IM_ARRAYSIZE(boneModes));
	DebugUI::DrawShadowPanel(engine.GetScene(), engine.GetRenderer(), engine.GetDevice());
	{
		// 컬링 고정 디버그 (docs/plan/freeze-culling-camera-plan.html): 지금 카메라 사본으로 켜 둔 항목을 계산하고, 에디터
		// 카메라를 옮겨 바깥에서 결과를 봄. 캐스케이드는 Debug view = Shadow cascade 와 같이 쓰면 색 띠가 보임.
		// 씬 불러오기·재생 시작·정지에서 자동으로 풀림 (EditorApp::ReleaseCullFreeze).
		Renderer& renderer = engine.GetRenderer();
		bool frozen = renderer.IsCullFrozen();
		if (ImGui::Checkbox("Freeze culling camera", &frozen))
		{
			renderer.SetCullFreeze(frozen, engine.GetCamera(), renderer.GetCullFreezeParts());   // 항목은 지난번 고른 값을 이어 씀
		}
		if (frozen)
		{
			ImGui::Indent();
			Renderer::CullFreezeParts parts = renderer.GetCullFreezeParts();
			bool changed = ImGui::Checkbox("Cascades", &parts.cascades);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("CSM splits and light views from the frozen camera (also the camera preview's shadows)");
			ImGui::SameLine();
			changed |= ImGui::Checkbox("Objects", &parts.objects);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("frustum-cull objects with the frozen camera (main view only)");
			ImGui::SameLine();
			changed |= ImGui::Checkbox("Lights", &parts.lights);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("cull local lights and pick shadow lights with the frozen camera; culled lights go dark.\nAlso applies to the camera preview. No effect on light while Clustered is off.");
			if (changed)
			{
				renderer.SetCullFreezeParts(parts);
			}
			ImGui::Unindent();
			const Renderer::Stats& stats = renderer.GetStats();
			ImGui::TextDisabled("frozen view: culled draws %u, lights visible %u / culled %u", stats.culled, stats.lightsVisible, stats.lightsCulled);
		}
		ImGui::TextDisabled("Freeze 후 에디터 카메라를 옮겨 보세요. 흰 절두체 = 고정 카메라, 색 절두체 = 캐스케이드, 회색 아이콘 = 컬링된 조명.");
	}
	DebugUI::DrawCameraPanel(engine.GetCamera(), cameraMoveSpeed);
	DebugUI::DrawEnginePanel(engine);
	DebugUI::EndPanel();
}

void Editor::DrawStats(Engine& engine, const char* sceneName, const ModelStats& modelStats)
{
	DebugUI::BeginPanel("Stats");
	const Renderer::Stats& stats = engine.GetRenderer().GetStats();
	const Time& time = engine.GetTime();
	ImGui::TextWrapped("%.1f FPS   %.2f ms/frame   scene view %ux%u", time.GetFps(), time.GetAverageFrameMs(), m_sceneWidth, m_sceneHeight);
	ImGui::TextWrapped("draws %u (+%u shadow, %u views)   draw items %u, culled main %u / shadow %u   triangles %u (main only)   PSO switches %u   set switches %u   mesh switches %u   PSO cache %zu", stats.draws, stats.shadowDraws, stats.shadowViews, stats.drawItems, stats.culled, stats.shadowCulled, stats.triangles, stats.pipelineSwitches, stats.resourceSetSwitches, stats.meshSwitches, engine.GetRenderer().GetPipelineCount());
	if (stats.particleEmitters > 0)
	{
		ImGui::TextWrapped("particles: emitters %u   capacity %u (only alive ones are drawn)   spawn requested this frame %u", stats.particleEmitters, stats.particleCapacity, stats.particlesSpawned);
	}
	if (stats.rainActive)
	{
		ImGui::TextWrapped("rain: drops %u   splash cells %u   wetness %.2f   occlusion map draws %u (culled %u)", stats.rainDrops, stats.rainSplashes, stats.rainWetness, stats.rainOcclusionDraws, stats.rainOcclusionCulled);
	}
	DebugUI::DrawModelPanel(sceneName, modelStats, engine.GetScene().GetImageCount(), engine.GetScene().GetObjects().size());
	DebugUI::DrawProfilerPanel();
	DebugUI::EndPanel();
}

#include "pch.h"
#include "App/Editor.h"
#include "App/DebugUI.h"
#include "App/ScriptCreator.h"
#include "App/GameBuilder.h"
#include "App/ProjectLauncher.h"
#include "Core/Engine.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "Core/Paths.h"
#include "Core/AssetManager.h"
#include "Graphics/Mesh.h"
#include "Graphics/Model.h"
#include "Scene/CameraComponent.h"
#include "RHI/Device.h"
#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder (아직 내부 API)
#include <ImGuizmo.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>        // C++20: 드롭 라벨 서식
#include <string_view>
#include <unordered_map>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	// 회전 행렬 → 오일러 각 변환은 11-F단계에 Transform::EulerFromRotationMatrix 로 옮겨 감 (재부모화·FollowTarget 도 같은 식을 쓰기 때문).

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
			if (t1 > t2) std::swap(t1, t2);
			tMin = (std::max)(tMin, t1);
			tMax = (std::min)(tMax, t2);
			if (tMin > tMax) return false;
		}
		outT = tMin;
		return true;
	}

	// 11-B단계: 씬 뷰 픽셀 (u, v ∈ 0..1) → 월드 광선. near/far 점을 역투영함 (Pick 과 드롭 위치 계산이 함께 씀).
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

	std::string FileStem(const std::wstring& relativePath)
	{
		std::string s = Log::ToUtf8(relativePath.c_str());
		const size_t slash = s.find_last_of("\\/");
		if (slash != std::string::npos) s = s.substr(slash + 1);
		const size_t dot = s.find_last_of('.');
		if (dot != std::string::npos && dot > 0) s = s.substr(0, dot);
		return s;
	}
}

Editor::Editor()
{
}

void Editor::Draw(Engine& engine, const char* sceneName, const ModelStats& modelStats, const Callbacks& callbacks)
{
	if (scenePath.empty()) scenePath = Paths::GetExecutableDir() + L"Scenes\\scene.json";

	// ---- 도킹 공간 (메인 뷰포트 전체, 메뉴 바 아래) ----
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImGuiID dockspaceId = ImGui::GetID("SherlockDockspace");
	if (!m_layoutBuilt && ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
	{
		BuildDefaultLayout(dockspaceId);
	}
	m_layoutBuilt = true;
	ImGui::DockSpaceOverViewport(dockspaceId, viewport);

	// 11-B단계: 콘텐츠 브라우저 에셋을 끌고 있는지 확인. IsDragDropActive 는 internal 이라 페이로드로 판단함.
	const ImGuiPayload* dragPayload = ImGui::GetDragDropPayload();
	m_assetDragActive = dragPayload != nullptr && dragPayload->IsDataType(kAssetPayloadType);

	DrawMenuBar(engine, callbacks);

	// 단축키: Ctrl+S 저장, Ctrl+L 로드, 1/2/3 기즈모 (텍스트 입력 중이 아닐 때)
	if (!ImGui::GetIO().WantTextInput)
	{
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && callbacks.saveScene) m_lastMessage = callbacks.saveScene(scenePath) ? "saved" : "save failed";
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L) && callbacks.loadScene) { m_lastMessage = callbacks.loadScene(scenePath) ? "loaded" : "load failed"; m_selected = -1; }
		if (ImGui::IsKeyPressed(ImGuiKey_1, false)) m_gizmoOperation = 0;
		if (ImGui::IsKeyPressed(ImGuiKey_2, false)) m_gizmoOperation = 1;
		if (ImGui::IsKeyPressed(ImGuiKey_3, false)) m_gizmoOperation = 2;
		// 11-C단계: Ctrl+P 재생/정지 (언리얼 Alt+P, 유니티 Ctrl+P)
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P))
		{
			if (playState == PlayState::Editing) { if (callbacks.play) callbacks.play(); }
			else if (callbacks.stop) callbacks.stop();
		}
	}
	DrawSceneView(engine, callbacks);
	DrawHierarchy(engine, callbacks);
	DrawInspector(engine);
	DrawRenderSettings(engine);
	DrawStats(engine, sceneName, modelStats);
	DrawContentBrowser(engine, callbacks);   // Stats 다음에 그림 — Stats 창의 도킹 노드에 따라 붙음

	ImGui::Begin("Lights");
	DebugUI::DrawEnvironmentPanel(engine.GetScene());   // 2026-10-08: 스카이박스·IBL — 주변광과 같은 "환경" 값이라 조명 창에 둠
	DebugUI::DrawLightPanel(engine.GetScene());
	ImGui::End();

	ImGui::Begin("Materials");
	DebugUI::DrawMaterialPanel(engine.GetScene());
	ImGui::End();

	if (showConsole) DebugUI::DrawLogConsole(&showConsole);
}

void Editor::BuildDefaultLayout(uint32_t dockspaceId)
{
	// 첫 실행(imgui.ini 없음)의 기본 배치: 왼쪽 Hierarchy, 가운데 Scene, 오른쪽 Inspector/Render Settings/Lights/Materials, 아래 Console/Stats.
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
	ImGui::DockBuilderDockWindow("Content Browser", bottom);   // 11-B단계: 같은 노드의 탭. 첫 프레임에 활성 탭으로 만듦
	ImGui::DockBuilderFinish(dockspaceId);
	m_focusContentBrowserFrames = 3;
}

void Editor::DrawMenuBar(Engine& engine, const Callbacks& callbacks)
{
	if (!ImGui::BeginMainMenuBar()) return;
	if (ImGui::BeginMenu("File"))
	{
		// 11-E단계: 프로젝트
		if (ImGui::MenuItem("New Project...")) m_openLauncher = true;
		if (ImGui::MenuItem("Open Project...") && callbacks.openProject)
		{
			const std::wstring path = ProjectLauncher::BrowseForProjectFile(nullptr);
			if (!path.empty()) m_lastMessage = callbacks.openProject(path) ? "project opened" : "open project failed";
		}
		if (ImGui::BeginMenu("Recent Projects"))
		{
			const std::vector<ProjectLauncher::Recent> recent = ProjectLauncher::LoadRecent();
			if (recent.empty()) ImGui::TextDisabled("(none)");
			for (const ProjectLauncher::Recent& r : recent)
			{
				const std::string label = r.name + "   " + Log::ToUtf8(r.path.c_str());
				if (ImGui::MenuItem(label.c_str()) && callbacks.openProject) m_lastMessage = callbacks.openProject(r.path) ? "project opened" : "open project failed";
			}
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Project Settings...", nullptr, false, project.loaded)) m_openProjectSettings = true;
		if (ImGui::MenuItem("Projects (launcher)...")) m_openLauncher = true;
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
			if (length > 1) MultiByteToWideChar(CP_UTF8, 0, m_pathBuffer, -1, &scenePath[0], length);
		}
		if (ImGui::MenuItem("Save scene", "Ctrl+S") && callbacks.saveScene)
		{
			m_lastMessage = callbacks.saveScene(scenePath) ? "saved" : "save failed";
		}
		if (ImGui::MenuItem("Load scene", "Ctrl+L") && callbacks.loadScene)
		{
			m_lastMessage = callbacks.loadScene(scenePath) ? "loaded" : "load failed";
			m_selected = -1;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("New scene (floor only)") && callbacks.newScene) { callbacks.newScene(); m_selected = -1; m_lastMessage = "new scene"; }
		ImGui::Separator();
		if (ImGui::MenuItem("Demo scene") && callbacks.switchScene) { callbacks.switchScene(0); m_selected = -1; }
		if (ImGui::MenuItem("DamagedHelmet") && callbacks.switchScene) { callbacks.switchScene(1); m_selected = -1; }
		if (ImGui::MenuItem("Sponza") && callbacks.switchScene) { callbacks.switchScene(2); m_selected = -1; }
		ImGui::Separator();
		if (ImGui::MenuItem("Build Game...")) m_openBuildPopup = true;   // 11-D단계
		ImGui::EndMenu();
	}
	if (m_openBuildPopup) { ImGui::OpenPopup("Build Game"); m_openBuildPopup = false; }
	DrawBuildPopup();
	if (m_openProjectSettings) { ImGui::OpenPopup("Project Settings"); m_openProjectSettings = false; }
	DrawProjectSettingsPopup(callbacks);
	if (m_openLauncher) { ImGui::OpenPopup("Projects"); m_openLauncher = false; }
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
		if (ImGui::RadioButton("Local space", !m_gizmoWorld)) m_gizmoWorld = false;
		if (ImGui::RadioButton("World space", m_gizmoWorld)) m_gizmoWorld = true;
		ImGui::EndMenu();
	}
	ImGui::Text("   %s   |   %s   |   backend %s   |   %s", m_lastMessage.c_str(), project.loaded ? project.name.c_str() : "(no project)", engine.GetDevice().GetBackendName(),
		m_selected >= 0 && m_selected < static_cast<int>(engine.GetScene().GetObjects().size()) ? engine.GetScene().GetObjects()[m_selected]->GetName().c_str() : "(no selection)");
	ImGui::EndMainMenuBar();
}

void Editor::DrawSceneView(Engine& engine, const Callbacks& callbacks)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Scene");
	ImGui::PopStyleVar();

	// 툴바 한 줄. 11-C단계: 재생 버튼이 맨 앞.
	ImGui::SetCursorPos(ImVec2(6.0f, 4.0f + ImGui::GetCursorPosY()));
	DrawPlayControls(callbacks);
	ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
	ImGui::RadioButton("Move", &m_gizmoOperation, 0); ImGui::SameLine();
	ImGui::RadioButton("Rotate", &m_gizmoOperation, 1); ImGui::SameLine();
	ImGui::RadioButton("Scale", &m_gizmoOperation, 2); ImGui::SameLine();
	ImGui::TextDisabled("|"); ImGui::SameLine();
	if (ImGui::RadioButton("Local", !m_gizmoWorld)) m_gizmoWorld = false; ImGui::SameLine();   // 기본: 오브젝트 축 기준
	if (ImGui::RadioButton("World", m_gizmoWorld)) m_gizmoWorld = true; ImGui::SameLine();
	ImGui::TextDisabled("RMB: look  WASD: move  LMB: select  1/2/3: gizmo  Ctrl+P: play");

	// 11-E단계 (2026-10-08 B안으로 조건 변경): 스크립트 DLL 이 안 올라왔거나 새 빌드가 있으면 씬 뷰 위(이미지 앞)에 배너를 표시 — 이미지가 남은 공간을 다 쓰므로 이미지보다 먼저 그림
	DrawProjectBanner(callbacks);

	const ImVec2 available = ImGui::GetContentRegionAvail();
	const uint32_t width = static_cast<uint32_t>((std::max)(available.x, 8.0f));
	const uint32_t height = static_cast<uint32_t>((std::max)(available.y, 8.0f));
	m_sceneWidth = width;
	m_sceneHeight = height;
	Renderer& renderer = engine.GetRenderer();
	renderer.SetSceneTarget(width, height);   // 크기가 같으면 아무것도 하지 않음
	if (width > 0 && height > 0) engine.GetCamera().SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));

	const ImVec2 imagePos = ImGui::GetCursorScreenPos();
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

	// 11-C단계: 재생 중 표시 — 언리얼 PIE 처럼 씬 뷰에 테두리를 그림 (재생 초록, 일시정지 노랑)
	if (playState != PlayState::Editing)
	{
		const ImU32 color = playState == PlayState::Playing ? IM_COL32(70, 200, 90, 255) : IM_COL32(240, 190, 40, 255);
		ImGui::GetWindowDrawList()->AddRect(imagePos, ImVec2(imagePos.x + width, imagePos.y + height), color, 0.0f, 0, 3.0f);
		const char* text = playState == PlayState::Playing ? "PLAYING" : "PAUSED";
		ImGui::GetWindowDrawList()->AddText(ImVec2(imagePos.x + 10.0f, imagePos.y + 8.0f), color, text);
	}

	// 11-B단계: 콘텐츠 브라우저 드롭 타깃. 이미지 아이템 바로 뒤에 있어야 함.
	// AcceptBeforeDelivery: 호버 중 매 프레임 페이로드를 받아 고스트 마커를 그리고, IsDelivery() 인 프레임(릴리스)에만 실제로 놓음.
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
			if (payload->IsDelivery()) HandleAssetDrop(engine, callbacks, asset, point, hitObject);
		}
		ImGui::EndDragDropTarget();
	}

	// 11-C단계: 씬 안의 카메라 오브젝트를 프러스텀 아이콘으로 표시 (편집 중에만 — 재생 중에는 그 카메라로 보고 있음)
	DrawCameraGizmos(engine, imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height));
	// 선택한 카메라의 시점 미리보기 (우측 하단)
	DrawCameraPreview(engine, imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height));

	// 선택된 오브젝트의 기즈모. 이미지 위에 그림. 에셋을 끌고 있는 동안은 끔.
	ImGuizmo::Enable(!m_assetDragActive);
	DrawGizmo(engine, imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height));
	ImGuizmo::Enable(true);

	// 클릭 선택. 기즈모를 잡고 있거나 커서가 기즈모 위에 있으면 제외.
	if (m_sceneHovered && !m_assetDragActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
	{
		const ImVec2 mouse = ImGui::GetMousePos();
		const float u = (mouse.x - imagePos.x) / static_cast<float>(width);
		const float v = (mouse.y - imagePos.y) / static_cast<float>(height);
		m_selected = Pick(engine.GetScene(), engine.GetCamera(), u, v);
	}
	ImGui::End();
}

void Editor::DrawGizmo(Engine& engine, float x, float y, float width, float height)
{
	m_gizmoUsing = false;
	Scene& scene = engine.GetScene();
	if (m_selected < 0 || m_selected >= static_cast<int>(scene.GetObjects().size()) || width <= 0.0f || height <= 0.0f) return;

	GameObject& object = *scene.GetObjects()[m_selected];
	const Camera& camera = engine.GetCamera();

	// DirectXMath 행벡터 행렬과 ImGuizmo(열벡터, 열우선 메모리)는 메모리 배치가 같음 — 이동 성분이 [12..14] 에 있는 것이 그 증거임.
	XMFLOAT4X4 view, proj, world;
	XMStoreFloat4x4(&view, camera.GetViewMatrix());
	XMStoreFloat4x4(&proj, camera.GetProjectionMatrix());
	XMStoreFloat4x4(&world, object.GetWorldMatrix());   // 11-F단계: 부모까지 곱한 월드 행렬. 기즈모는 언제나 월드 공간에서 조작함

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(x, y, width, height);
	const ImGuizmo::OPERATION operation = m_gizmoOperation == 0 ? ImGuizmo::TRANSLATE : (m_gizmoOperation == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE);
	const ImGuizmo::MODE mode = (m_gizmoWorld && m_gizmoOperation != 2) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
	if (ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], operation, mode, &world.m[0][0]))
	{
		// world = pre × S·R·T × parent.world 이므로 GameObject 가 parent.world⁻¹ 을, Transform 이 pre⁻¹ 과 S·R·T 분해를 맡음 (11-F단계).
		// 분해가 실패하면(스케일 0) 이번 프레임 조작은 버려짐.
		object.SetWorldMatrix(XMLoadFloat4x4(&world));
	}
	m_gizmoUsing = ImGuizmo::IsUsing();
}

int Editor::Pick(Scene& scene, const Camera& camera, float u, float v, float* outDistance) const
{
	if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return -1;
	// 픽셀 → NDC → 월드 광선 (near 와 far 점을 역투영). 11-B단계에 ScreenToRay 로 분리.
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
			// 11-C단계: 메시 없는 카메라 오브젝트는 1 단위 상자로 판정함 (씬 뷰의 카메라 아이콘과 같은 자리)
			boxMin = XMFLOAT3(-0.5f, -0.5f, -0.5f);
			boxMax = XMFLOAT3(0.5f, 0.5f, 0.5f);
		}
		else continue;
		// 광선을 오브젝트 로컬 공간으로 옮겨 로컬 AABB 와 검사함 (회전·스케일이 있어도 정확).
		const XMMATRIX world = objects[i]->GetWorldMatrix();   // 11-F단계: 계층 포함
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
	if (outDistance != nullptr && best >= 0) *outDistance = bestT;
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

// ------------------------------------------------------------------ 11-B단계: 콘텐츠 브라우저 · 드롭

void Editor::DrawContentBrowser(Engine& engine, const Callbacks& callbacks)
{
	if (!showContentBrowser) return;
	// 저장된 imgui.ini 에 이 창이 없으면(11단계 레이아웃) Stats 가 있는 노드의 탭으로 처음 한 번 붙임.
	// FirstUseEver 이므로 ini 에 항목이 생긴 뒤에는 사용자가 옮긴 자리를 존중함. 도킹 ID 는 바꾸지 않음.
	if (ImGuiWindow* stats = ImGui::FindWindowByName("Stats"))
	{
		if (stats->DockId != 0) ImGui::SetNextWindowDockID(stats->DockId, ImGuiCond_FirstUseEver);
	}
	ImGui::Begin("Content Browser", &showContentBrowser);
	if (m_focusContentBrowserFrames > 0)
	{
		ImGui::SetWindowFocus();
		--m_focusContentBrowserFrames;
	}
	ContentBrowser::Actions actions;
	actions.openScene = [&](const std::wstring& relativePath) { LoadSceneFromAsset(callbacks, relativePath); };
	actions.placeModel = [&](const std::wstring& relativePath, const XMFLOAT3& position) { PlaceModelFromAsset(engine, callbacks, relativePath, position); };
	m_contentBrowser.Draw(engine.GetDevice(), actions);
	ImGui::End();
}

void Editor::PlaceModelFromAsset(Engine& engine, const Callbacks& callbacks, const std::wstring& relativePath, const XMFLOAT3& position)
{
	if (!callbacks.placeModel) return;
	const size_t before = engine.GetScene().GetObjects().size();
	callbacks.placeModel(relativePath, position);
	const size_t after = engine.GetScene().GetObjects().size();
	if (after > before)
	{
		m_selected = static_cast<int>(after) - 1;
		m_lastMessage = "placed " + FileStem(relativePath);
	}
	else
	{
		m_lastMessage = "place failed: " + FileStem(relativePath);
	}
}

void Editor::LoadSceneFromAsset(const Callbacks& callbacks, const std::wstring& relativePath)
{
	if (!callbacks.loadScene) return;
	scenePath = Paths::GetAssetRoot() + relativePath;
	m_pathBuffer[0] = '\0';   // File 메뉴의 경로 필드를 다음에 열 때 다시 채움
	m_lastMessage = callbacks.loadScene(scenePath) ? "loaded " + FileStem(relativePath) : "load failed";
	m_selected = -1;
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
	case AssetType::Texture:
	{
		// 광선에 맞은 오브젝트의 재질 알베도로 지정. 재질은 공유될 수 있어 같은 재질을 쓰는 오브젝트가 모두 바뀜 (메시지로 알림).
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
		for (auto& m : scene.GetMaterials()) if (m.get() == object.GetMaterial()) material = m.get();
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
	const auto project = [&](const XMFLOAT3& p, ImVec2& out) -> bool { return ProjectToView(viewProj, x, y, width, height, p, out); };
	const auto line = [&](const XMFLOAT3& a, const XMFLOAT3& b, ImU32 color, float thickness)
	{
		ImVec2 pa, pb;
		if (project(a, pa) && project(b, pb)) dl->AddLine(pa, pb, color, thickness);
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
					c[i].x += offset.x; c[i].y += offset.y; c[i].z += offset.z;
				}
				static const int edges[12][2] = { {0,1},{1,3},{3,2},{2,0}, {4,5},{5,7},{7,6},{6,4}, {0,4},{1,5},{2,6},{3,7} };
				for (const auto& e : edges) line(c[e[0]], c[e[1]], accentSoft, 1.5f);
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

void Editor::DrawHierarchy(Engine& engine, const Callbacks& callbacks)
{
	ImGui::Begin("Hierarchy");
	Scene& scene = engine.GetScene();
	auto& objects = scene.GetObjects();
	ImGui::Text("Objects %zu   Lights %zu", objects.size(), scene.GetLights().size());
	// 추가/삭제. 새 오브젝트는 원점 위에 기본 재질로 생기고 바로 선택됨.
	if (ImGui::SmallButton("+ Sphere") && callbacks.addPrimitive) { callbacks.addPrimitive(0); m_selected = static_cast<int>(objects.size()) - 1; }
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Cube") && callbacks.addPrimitive) { callbacks.addPrimitive(1); m_selected = static_cast<int>(objects.size()) - 1; }
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Cylinder") && callbacks.addPrimitive) { callbacks.addPrimitive(2); m_selected = static_cast<int>(objects.size()) - 1; }
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Plane") && callbacks.addPrimitive) { callbacks.addPrimitive(3); m_selected = static_cast<int>(objects.size()) - 1; }
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Camera") && callbacks.addPrimitive) { callbacks.addPrimitive(4); m_selected = static_cast<int>(objects.size()) - 1; }   // 11-C단계: 현재 에디터 시점 그대로 카메라 오브젝트 추가
	const bool canDelete = m_selected >= 0 && m_selected < static_cast<int>(objects.size());
	const bool deleteKey = canDelete && (m_sceneFocused || ImGui::IsWindowFocused()) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false);
	if (canDelete) { ImGui::SameLine(); }
	if (canDelete && (ImGui::SmallButton("Delete (Del)") || deleteKey) && callbacks.deleteObject) { callbacks.deleteObject(m_selected); m_selected = -1; }
	ImGui::Separator();
	// 11-F단계: 평면 목록 대신 트리. 루트(부모 없음)부터 그리고 자식은 재귀. 선택은 여전히 씬 벡터의 인덱스이므로 포인터 → 인덱스 표를 한 번 만든다.
	// 드래그 앤 드롭으로 재부모화: 항목을 다른 항목 위에 놓으면 그 자식이 되고(월드 자세 유지), 빈 곳에 놓으면 루트가 된다.
	std::unordered_map<const GameObject*, int> indexOf;
	indexOf.reserve(objects.size());
	for (int i = 0; i < static_cast<int>(objects.size()); ++i) indexOf[objects[i].get()] = i;
	for (int i = 0; i < static_cast<int>(objects.size()); ++i)
		if (objects[i]->GetParent() == nullptr) DrawHierarchyNode(scene, *objects[i], indexOf);
	// 빈 공간에 드롭 → 루트로
	ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, (std::max)(ImGui::GetContentRegionAvail().y, 24.0f)));
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SHERLOCK_OBJECT"))
		{
			const int dragged = *static_cast<const int*>(payload->Data);
			if (GameObject* object = scene.GetObject(dragged < 0 ? SIZE_MAX : static_cast<size_t>(dragged)))
			{
				if (object->SetParent(nullptr)) Log::Info("계층: '%s' 를 루트로", object->GetName().c_str());
			}
		}
		ImGui::EndDragDropTarget();
	}
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) m_selected = -1;
	ImGui::End();
}

void Editor::DrawHierarchyNode(Scene& scene, GameObject& object, const std::unordered_map<const GameObject*, int>& indexOf)
{
	const auto found = indexOf.find(&object);
	const int index = found != indexOf.end() ? found->second : -1;
	ImGui::PushID(&object);
	std::string label = object.GetName().empty() ? "(unnamed)" : object.GetName();
	if (!object.GetBehaviours().empty()) label += "  [" + std::to_string(object.GetBehaviours().size()) + "]";   // 11-C단계: 컴포넌트 수
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	// TreePop 여부는 TreeNodeEx 를 부르는 시점의 자식 유무로 고정함. 아래 드롭 처리에서 SetParent 가 자식 목록을 바꾸므로,
	// 잎(NoTreePushOnOpen — 푸시 안 함)으로 그린 노드가 같은 프레임에 자식을 얻으면 다시 물어봤을 때 짝 없는 TreePop 이 나감.
	const bool hadChildren = !object.GetChildren().empty();
	if (!hadChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	if (m_selected == index) flags |= ImGuiTreeNodeFlags_Selected;
	const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_selected = index;

	// 드래그 소스: 페이로드는 씬 인덱스. 드롭 대상: 이 오브젝트의 자식으로 (자기 자신·자손이면 SetParent 가 거부).
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	{
		ImGui::SetDragDropPayload("SHERLOCK_OBJECT", &index, sizeof(index));
		ImGui::TextUnformatted(object.GetName().c_str());
		ImGui::EndDragDropSource();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SHERLOCK_OBJECT"))
		{
			const int dragged = *static_cast<const int*>(payload->Data);
			if (GameObject* child = scene.GetObject(dragged < 0 ? SIZE_MAX : static_cast<size_t>(dragged)))
			{
				if (child->SetParent(&object)) Log::Info("계층: '%s' → '%s' 의 자식", child->GetName().c_str(), object.GetName().c_str());
				else Log::Warn("계층: '%s' 를 '%s' 의 자식으로 만들 수 없음 (자기 자신 또는 자손)", child->GetName().c_str(), object.GetName().c_str());
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (open && hadChildren)
	{
		// 자식 목록을 복사해 순회함: 드롭으로 재부모화하면 순회 중인 벡터가 바뀔 수 있음.
		const std::vector<GameObject*> children = object.GetChildren();
		for (GameObject* child : children) DrawHierarchyNode(scene, *child, indexOf);
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void Editor::DrawInspector(Engine& engine)
{
	ImGui::Begin("Inspector");
	Scene& scene = engine.GetScene();
	if (m_selected < 0 || m_selected >= static_cast<int>(scene.GetObjects().size()))
	{
		ImGui::TextDisabled("Click an object in the Scene view or the Hierarchy.");
		ImGui::End();
		return;
	}
	GameObject& object = *scene.GetObjects()[m_selected];
	ImGui::Text("%s", object.GetName().c_str());
	if (playState != PlayState::Editing) ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.15f, 1.0f), "Playing: edits are discarded on Stop");   // 11-C단계
	ImGui::Separator();

	Transform& t = object.GetTransform();
	XMFLOAT3 position = t.GetPosition();
	XMFLOAT3 rotation = t.GetRotation();
	XMFLOAT3 scale = t.GetScale();
	XMFLOAT3 rotationDeg(XMConvertToDegrees(rotation.x), XMConvertToDegrees(rotation.y), XMConvertToDegrees(rotation.z));
	if (ImGui::DragFloat3("Position", &position.x, 0.05f)) t.SetPosition(position);
	if (ImGui::DragFloat3("Rotation (deg)", &rotationDeg.x, 0.5f))
	{
		t.SetRotation(XMFLOAT3(XMConvertToRadians(rotationDeg.x), XMConvertToRadians(rotationDeg.y), XMConvertToRadians(rotationDeg.z)));
	}
	if (ImGui::DragFloat3("Scale", &scale.x, 0.01f, 0.001f, 1000.0f)) t.SetScale(scale);
	if (t.HasPreTransform()) ImGui::TextDisabled("(model node pre-transform applied before S*R*T)");
	// 11-F단계: 부모가 있으면 위 값은 부모 기준 로컬임을 알리고 월드 위치를 함께 보임. Detach 는 월드 자세를 유지한 채 루트로 올림.
	if (const GameObject* parent = object.GetParent())
	{
		const XMFLOAT3 wp = object.GetWorldPosition();
		ImGui::TextDisabled("local to parent '%s'   world (%.2f, %.2f, %.2f)", parent->GetName().c_str(), wp.x, wp.y, wp.z);
		ImGui::SameLine();
		if (ImGui::SmallButton("Detach")) object.SetParent(nullptr);
	}
	else if (!object.GetChildren().empty()) ImGui::TextDisabled("%zu children follow this object (drag in Hierarchy to reparent)", object.GetChildren().size());

	// 11-C단계: 카메라 오브젝트 — 에디터 시점과 자세를 주고받음
	if (CameraComponent* cameraComponent = object.GetBehaviour<CameraComponent>())
	{
		if (ImGui::Button("Look through")) cameraComponent->ApplyTo(engine.GetCamera(), false);   // 에디터 시점 ← 이 카메라 (렌즈는 그대로)
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("move the editor view to this camera");
		ImGui::SameLine();
		if (ImGui::Button("Align to view")) cameraComponent->SetFromCamera(engine.GetCamera());   // 이 카메라 ← 에디터 시점
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("place this camera where the editor view is");
		if (scene.GetActiveCamera() == cameraComponent) ImGui::TextColored(ImVec4(0.3f, 0.75f, 0.35f, 1.0f), "active camera");
		else if (playState == PlayState::Editing) ImGui::TextDisabled("rendered through during Play (highest priority wins)");
	}

	if (const Mesh* mesh = object.GetMesh())
	{
		const MeshSource* source = scene.GetMeshSource(mesh);
		const char* type = "custom";
		if (source != nullptr)
		{
			switch (source->type)
			{
			case MeshSource::Type::Sphere: type = "sphere"; break;
			case MeshSource::Type::Cube: type = "cube"; break;
			case MeshSource::Type::Plane: type = "plane"; break;
			case MeshSource::Type::Cylinder: type = "cylinder"; break;
			case MeshSource::Type::Model: type = "model"; break;
			default: break;
			}
		}
		ImGui::Separator();
		ImGui::Text("Mesh: %s   vertices %u   triangles %u   submeshes %zu", type, mesh->GetVertexCount(), mesh->GetIndexCount() / 3, mesh->GetSubmeshes().size());
		if (source != nullptr && source->type == MeshSource::Type::Model) ImGui::TextDisabled("%s #%u", Log::ToUtf8(source->modelPath.c_str()).c_str(), source->meshIndex);
	}

	// 재질: 씬의 재질 중에서 고르고 그 자리에서 편집함.
	ImGui::Separator();
	std::vector<std::unique_ptr<Material>>& materials = scene.GetMaterials();
	int current = -1;
	for (int i = 0; i < static_cast<int>(materials.size()); ++i) if (materials[i].get() == object.GetMaterial()) current = i;
	const char* currentName = current >= 0 ? materials[current]->name.c_str() : "(renderer default)";
	if (ImGui::BeginCombo("Material", currentName))
	{
		for (int i = 0; i < static_cast<int>(materials.size()); ++i)
		{
			ImGui::PushID(i);
			if (ImGui::Selectable(materials[i]->name.c_str(), i == current)) object.SetMaterial(materials[i].get());
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	if (!object.GetSlotMaterials().empty()) ImGui::TextDisabled("submesh slots: %zu (edit in Materials window)", object.GetSlotMaterials().size());
	if (current >= 0)
	{
		Material& material = *materials[current];
		ImGui::ColorEdit4("Base color", &material.baseColor.x);
		ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f);   // 2026-10-08 PBR
		ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f);
		ImGui::ColorEdit3("Emissive", &material.emissive.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
		ImGui::DragFloat2("UV scale", &material.uvScale.x, 0.1f, 0.1f, 64.0f);
		ImGui::SliderFloat("Normal strength", &material.normalStrength, 0.0f, 3.0f);
		ImGui::SliderFloat("Alpha cutoff", &material.alphaCutoff, 0.0f, 1.0f);
		ImGui::Checkbox("Unlit", &material.unlit); ImGui::SameLine();
		ImGui::Checkbox("Wireframe", &material.wireframe); ImGui::SameLine();
		ImGui::Checkbox("Double sided", &material.doubleSided);
		ImGui::TextDisabled("albedo: %s   normal: %s", material.albedoTexture.empty() ? "(none)" : material.albedoTexture.c_str(),
			material.normalTexture.empty() ? "(none)" : material.normalTexture.c_str());
	}

	// 11-C단계: 컴포넌트
	ImGui::Separator();
	DrawComponents(object);
	ImGui::End();
}

// ------------------------------------------------------------------ 11-C단계: 재생 · 컴포넌트

namespace
{
	// Behaviour::Reflect 의 필드를 ImGui 위젯으로 그림. Scene 계층은 ImGui 를 모르므로 방문자는 에디터 쪽에 둠.
	class ImGuiPropertyVisitor : public PropertyVisitor
	{
	public:
		void Float(const char* name, float& value, float speed) override { ImGui::DragFloat(name, &value, speed); }
		void Int(const char* name, int& value) override { ImGui::DragInt(name, &value); }
		void Bool(const char* name, bool& value) override { ImGui::Checkbox(name, &value); }
		void Float3(const char* name, XMFLOAT3& value, float speed) override { ImGui::DragFloat3(name, &value.x, speed); }
		void String(const char* name, std::string& value) override
		{
			char buffer[256];
			strncpy_s(buffer, value.c_str(), sizeof(buffer) - 1);
			if (ImGui::InputText(name, buffer, sizeof(buffer))) value = buffer;
		}
	};
}

void Editor::DrawCameraGizmos(Engine& engine, float x, float y, float width, float height)
{
	if (playState != PlayState::Editing || width <= 0.0f || height <= 0.0f) return;
	Scene& scene = engine.GetScene();
	const Camera& editorCamera = engine.GetCamera();
	const XMMATRIX viewProj = editorCamera.GetViewMatrix() * editorCamera.GetProjectionMatrix();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->PushClipRect(ImVec2(x, y), ImVec2(x + width, y + height), true);   // 이미지 밖(툴바)으로 선이 새지 않게
	const auto project = [&](const XMFLOAT3& p, ImVec2& out) -> bool { return ProjectToView(viewProj, x, y, width, height, p, out); };
	const auto line = [&](const XMFLOAT3& a, const XMFLOAT3& b, ImU32 color, float thickness)
	{
		ImVec2 pa, pb;
		if (project(a, pa) && project(b, pb)) dl->AddLine(pa, pb, color, thickness);
	};

	auto& objects = scene.GetObjects();
	for (int i = 0; i < static_cast<int>(objects.size()); ++i)
	{
		CameraComponent* cameraComponent = objects[i]->GetBehaviour<CameraComponent>();
		if (cameraComponent == nullptr) continue;
		// 에디터 눈과 겹치는 카메라(방금 "Align to view" 한 것)는 프러스텀이 화면 테두리와 일치해 의미가 없음 — 건너뜀.
		const XMFLOAT3 eyeDelta(cameraComponent->GetPose().position.x - editorCamera.GetPosition().x,
			cameraComponent->GetPose().position.y - editorCamera.GetPosition().y, cameraComponent->GetPose().position.z - editorCamera.GetPosition().z);
		if (eyeDelta.x * eyeDelta.x + eyeDelta.y * eyeDelta.y + eyeDelta.z * eyeDelta.z < 0.01f) continue;
		const CameraPose pose = cameraComponent->GetPose();
		const XMMATRIX rotation = pose.GetRotationMatrix();   // 11-F단계: 월드 회전 (부모 roll 포함) 그대로
		const XMVECTOR eye = XMLoadFloat3(&pose.position);
		const XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotation);
		const XMVECTOR right = XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rotation);
		const XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), rotation);

		// 눈에서 1.5 단위 앞의 프러스텀 단면 (fov 는 컴포넌트 값, 종횡비는 씬 뷰 값). 위쪽의 작은 삼각형 = "위".
		const float depth = 1.5f;
		const float halfH = depth * tanf(pose.fovY * 0.5f);
		const float halfW = halfH * editorCamera.GetAspect();
		const XMVECTOR center = eye + forward * depth;
		XMFLOAT3 corner[4], apex, top;
		XMStoreFloat3(&corner[0], center - right * halfW + up * halfH);
		XMStoreFloat3(&corner[1], center + right * halfW + up * halfH);
		XMStoreFloat3(&corner[2], center + right * halfW - up * halfH);
		XMStoreFloat3(&corner[3], center - right * halfW - up * halfH);
		XMStoreFloat3(&apex, eye);
		XMStoreFloat3(&top, center + up * (halfH * 1.5f));

		const bool selected = i == m_selected;
		const ImU32 color = selected ? IM_COL32(255, 200, 40, 255) : IM_COL32(120, 190, 255, 220);
		const float thickness = selected ? 2.0f : 1.2f;
		for (int c = 0; c < 4; ++c)
		{
			line(apex, corner[c], color, thickness);
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
		? scene.GetObjects()[m_selected]->GetBehaviour<CameraComponent>() : nullptr;
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
	renderer.SetPreviewCamera(&m_previewCamera);   // 이번 프레임의 Render 가 이 카메라로 미리보기 패스를 그림

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
	// 2026-10-08 (B안): 스크립트는 exe 가 아니라 <이름>Scripts.dll 에 있음. 배너는 "스크립트가 있는데 DLL 이 안 올라옴" 과
	// "새 빌드가 있지만 재생 중이라 아직 못 올림" 두 경우에만 뜸. 스크립트가 없는 프로젝트(렌더링 실험 등)는 배너가 없음.
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
		ImGui::TextWrapped("Scripts of project '%s' are not loaded (%s). Objects using them keep working with the other components; build the scripts to use them.",
			project.name.c_str(), project.scriptsError.empty() ? "not built" : project.scriptsError.c_str());
		if (project.hasSolution)
		{
			if (ImGui::Button("Build scripts") && callbacks.buildScripts) callbacks.buildScripts();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("MSBuild %sScripts (%s|x64) in %s, then load the DLL without restarting", project.name.c_str(),
#if defined(_DEBUG)
				"Debug",
#else
				"Release",
#endif
				Log::ToUtf8(project.solutionPath.c_str()).c_str());
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
	if (m_newProjectParent.empty()) m_newProjectParent = ProjectLauncher::GetDefaultProjectsDir();

	ImGui::SeparatorText("Recent");
	const std::vector<ProjectLauncher::Recent> recent = ProjectLauncher::LoadRecent();
	if (recent.empty()) ImGui::TextDisabled("(no recent projects)");
	for (size_t i = 0; i < recent.size(); ++i)
	{
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::Button("Open")) { if (callbacks.openProject && callbacks.openProject(recent[i].path)) ImGui::CloseCurrentPopup(); else m_launcherMessage = "open failed"; }
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
		if (!path.empty()) { if (callbacks.openProject && callbacks.openProject(path)) ImGui::CloseCurrentPopup(); else m_launcherMessage = "open failed"; }
	}

	ImGui::SeparatorText("New");
	ImGui::SetNextItemWidth(220.0f);
	ImGui::InputText("Name", m_newProjectName, sizeof(m_newProjectName), ImGuiInputTextFlags_CharsNoBlank);
	ImGui::Text("Location: %s", Log::ToUtf8(m_newProjectParent.c_str()).c_str());
	ImGui::SameLine();
	if (ImGui::SmallButton("Browse..."))
	{
		const std::wstring dir = ProjectLauncher::BrowseForFolder(nullptr, m_newProjectParent);
		if (!dir.empty()) m_newProjectParent = dir + L"\\";
	}
	ImGui::TextDisabled("Creates <Location>\\<Name>\\ with Assets, Scripts, a Main.json scene and <Name>.sln (editor + game targets).");
	ImGui::BeginDisabled(!Project::IsValidName(m_newProjectName));
	if (ImGui::Button("Create and open") && callbacks.newProject)
	{
		std::string error;
		if (callbacks.newProject(m_newProjectParent, m_newProjectName, error)) { m_launcherMessage.clear(); ImGui::CloseCurrentPopup(); }
		else m_launcherMessage = error;
	}
	ImGui::EndDisabled();
	if (!m_launcherMessage.empty()) ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "%s", m_launcherMessage.c_str());
	ImGui::Separator();
	if (ImGui::Button("Close")) { m_launcherMessage.clear(); ImGui::CloseCurrentPopup(); }
	ImGui::EndPopup();
}

void Editor::DrawProjectSettingsPopup(const Callbacks& callbacks)
{
	if (!ImGui::BeginPopupModal("Project Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	ImGui::Text("%s", project.name.c_str());
	ImGui::TextDisabled("%s", Log::ToUtf8(project.root.c_str()).c_str());
	if (m_buildScenes.empty()) m_buildScenes = GameBuilder::ListScenes();
	if (projectStartScene != nullptr)
	{
		ImGui::SetNextItemWidth(260.0f);
		if (ImGui::BeginCombo("Start scene", projectStartScene->c_str()))
		{
			for (const std::string& scene : m_buildScenes)
				if (ImGui::Selectable(scene.c_str(), scene == *projectStartScene)) *projectStartScene = scene;
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("refresh")) m_buildScenes = GameBuilder::ListScenes();
	}
	ImGui::TextDisabled("Solution: %s", project.hasSolution ? Log::ToUtf8(project.solutionPath.c_str()).c_str() : "(none)");
	// 2026-10-08 (B안): 스크립트 DLL 상태와 빌드 버튼
	if (project.scriptsLoaded)
		ImGui::TextDisabled("Scripts DLL: loaded, %zu component(s), loaded %u time(s)", project.scriptTypes, project.scriptReloads);
	else
		ImGui::TextDisabled("Scripts DLL: not loaded (%s)", project.scriptsError.empty() ? "no scripts" : project.scriptsError.c_str());
	ImGui::TextDisabled("  %s", Log::ToUtf8(project.scriptsDll.c_str()).c_str());
	if (project.hasSolution && callbacks.buildScripts)
	{
		if (ImGui::Button("Build scripts")) callbacks.buildScripts();
		ImGui::SameLine();
		ImGui::TextDisabled("(%zu source file(s); a newer build is also picked up automatically while editing)", project.scriptSources);
	}
	ImGui::Separator();
	if (ImGui::Button("Save") && callbacks.saveProject) m_lastMessage = callbacks.saveProject() ? "project saved" : "project save failed";
	ImGui::SameLine();
	if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}

void Editor::DrawBuildPopup()
{
	// 11-D단계: 언리얼의 Package Project 에 해당. File 메뉴 항목은 플래그만 세우고, 메뉴를 닫은 뒤(EndMenu 뒤) OpenPopup 해서 여기서 그림.
	if (!ImGui::BeginPopupModal("Build Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	if (!GameBuilder::IsAvailable())
	{
		ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "no project is open (File > Projects)");
		if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}
	m_buildOptions.projectName = project.name;
	m_buildOptions.projectSolution = project.solutionPath;
	if (project.scriptSources > 0 && !project.scriptsLoaded) ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "warning: %sScripts.dll is not built; the package will not have the scripts (build them first)", project.name.c_str());
	if (m_buildScenes.empty()) m_buildScenes = GameBuilder::ListScenes();
	if (m_buildOptions.startScene.empty() && !m_buildScenes.empty())
	{
		// 기본: 지금 열린 씬 파일이 목록에 있으면 그것, 아니면 첫 항목
		const std::string current = FileStem(scenePath) + ".json";
		m_buildOptions.startScene = std::find(m_buildScenes.begin(), m_buildScenes.end(), current) != m_buildScenes.end() ? current : m_buildScenes.front();
	}
	char name[64];
	strncpy_s(name, m_buildOptions.name.c_str(), sizeof(name) - 1);
	ImGui::SetNextItemWidth(260.0f);
	if (ImGui::InputText("Game name", name, sizeof(name))) m_buildOptions.name = name;
	ImGui::SetNextItemWidth(260.0f);
	if (ImGui::BeginCombo("Start scene", m_buildOptions.startScene.c_str()))
	{
		for (const std::string& scene : m_buildScenes)
			if (ImGui::Selectable(scene.c_str(), scene == m_buildOptions.startScene)) m_buildOptions.startScene = scene;
		ImGui::EndCombo();
	}
	if (ImGui::SmallButton("refresh scenes")) m_buildScenes = GameBuilder::ListScenes();
	ImGui::TextDisabled("(save the scene first: Ctrl+S writes Assets\\Scenes)");
	ImGui::Checkbox("Copy all assets", &m_buildOptions.copyAllAssets);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("off: only Config, Textures and the models the start scene references (skips Sponza etc.)");
	ImGui::Checkbox("Build Release with MSBuild first", &m_buildOptions.releaseBuild);
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("runs MSBuild on the project's game target (Release|x64); the editor freezes until it finishes. Off: uses the runtime next to this editor");
	ImGui::Checkbox("Open folder when done", &m_buildOptions.openFolder);
	ImGui::TextDisabled("%s", Log::ToUtf8((GameBuilder::GetBuildRoot() + std::wstring(m_buildOptions.name.begin(), m_buildOptions.name.end()) + L"\\").c_str()).c_str());
	if (!m_buildMessage.empty()) ImGui::TextWrapped("%s", m_buildMessage.c_str());
	ImGui::Separator();
	if (ImGui::Button("Build"))
	{
		const GameBuilder::Result result = GameBuilder::Build(m_buildOptions);
		m_buildMessage = result.message;
		m_lastMessage = result.message;
		if (result.ok) m_buildMessage += "\n" + Log::ToUtf8(result.outputDir.c_str());
	}
	ImGui::SameLine();
	if (ImGui::Button("Close")) { m_buildMessage.clear(); ImGui::CloseCurrentPopup(); }
	ImGui::EndPopup();
}

void Editor::DrawPlayControls(const Callbacks& callbacks)
{
	const bool editing = playState == PlayState::Editing;
	const bool playing = playState == PlayState::Playing;
	if (editing)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.6f, 0.3f, 1.0f));
		if (ImGui::Button("Play") && callbacks.play) callbacks.play();
		ImGui::PopStyleColor();
	}
	else
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.25f, 0.25f, 1.0f));
		if (ImGui::Button("Stop") && callbacks.stop) callbacks.stop();
		ImGui::PopStyleColor();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(editing);
	if (ImGui::Button(playing ? "Pause" : "Resume") && callbacks.pauseToggle) callbacks.pauseToggle();
	ImGui::SameLine();
	if (ImGui::Button("Step") && callbacks.stepFrame) callbacks.stepFrame();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	ImGui::SliderFloat("##timeScale", &timeScale, 0.0f, 4.0f, "x%.2f");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("time scale (dt multiplier while playing)");
}

void Editor::DrawComponents(GameObject& object)
{
	auto& behaviours = object.GetBehaviours();
	ImGui::Text("Components (%zu)", behaviours.size());
	int removeIndex = -1;
	for (int i = 0; i < static_cast<int>(behaviours.size()); ++i)
	{
		Behaviour& behaviour = *behaviours[i];
		ImGui::PushID(i);
		if (ImGui::SmallButton("x")) removeIndex = i;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("remove component");
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
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("open Scripts/%s.h and .cpp (after editing: Build scripts, or build in Visual Studio — the editor reloads the DLL)", behaviour.GetTypeName());
			ImGui::SameLine();
		}
		const bool open = ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed, "%s%s", behaviour.GetTypeName(), isScript ? "  (Script)" : "");
		if (open)
		{
			ImGuiPropertyVisitor visitor;
			behaviour.Reflect(visitor);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (removeIndex >= 0) object.RemoveBehaviour(static_cast<size_t>(removeIndex));

	// 2026-10-08 (B안): 스크립트 DLL 이 없어 만들지 못한 컴포넌트. 값은 보관 중이며 Build scripts 뒤 핫리로드가 되살림
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
		if (ImGui::SmallButton("x")) removeMissing = i;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("remove (the saved values are lost)");
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s  (Missing script)", type.c_str());
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("not registered: build the project scripts. Values are kept and restored on reload.");
		ImGui::PopID();
	}
	if (removeMissing >= 0) missing.erase(missing.begin() + removeMissing);

	// 추가: 레지스트리에 등록된 C++ 클래스 목록. 스크립트(프로젝트의 Scripts, SHERLOCK_SCRIPT)와 엔진 컴포넌트를 나눠 보여 줌.
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
	if (BehaviourRegistry::GetTypeNames().empty()) ImGui::TextDisabled("(nothing registered — add a class in Scripts with SHERLOCK_SCRIPT)");

	// 새 스크립트 (유니티의 Create > C# Script 에 해당). 파일 생성과 편집기로 열기까지 함. 2026-10-08 (B안): Build scripts 하면 에디터가 DLL 을 다시 올려 목록에 나타남 (재실행 불필요).
	if (ImGui::Button("New Script...")) ImGui::OpenPopup("New Script");
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("create Scripts/<Name>.h + .cpp from a template and open them");
	if (ImGui::BeginPopupModal("New Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (!ScriptCreator::IsAvailable())
		{
			ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "source tree not found - run from the repository build (x64\\Debug)");
			if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		else
		{
			ImGui::TextUnformatted("Class name (C++ identifier, e.g. EnemyPatrol):");
			ImGui::SetNextItemWidth(320.0f);
			const bool enter = ImGui::InputText("##name", m_newScriptName, sizeof(m_newScriptName), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank);
			ImGui::TextDisabled("%s", Log::ToUtf8((ScriptCreator::GetScriptsDir() + L"<Name>.h  +  <Name>.cpp").c_str()).c_str());
			ImGui::TextDisabled("Both files open in your editor. Build scripts (or build in Visual Studio) - the editor reloads the DLL.");
			if (!m_newScriptError.empty()) ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "%s", m_newScriptError.c_str());
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
				else m_newScriptError = error;
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) { m_newScriptError.clear(); ImGui::CloseCurrentPopup(); }
			ImGui::EndPopup();
		}
	}
}

void Editor::DrawRenderSettings(Engine& engine)
{
	ImGui::Begin("Render Settings");
	DebugUI::DrawRenderSettingsPanel(engine.GetRenderer(), engine.GetDevice());
	RenderSettings& settings = engine.GetRenderer().GetSettings();
	const char* views[] = { "Lit", "Albedo", "World normal", "Depth", "Shadow factor", "UV", "Metal(B)/Rough(G)", "Occlusion" };
	ImGui::Combo("Debug view", &settings.debugView, views, IM_ARRAYSIZE(views));
	if (settings.debugView == 3) ImGui::SliderFloat("Depth range", &settings.debugDepthRange, 1.0f, 500.0f);
	DebugUI::DrawShadowPanel(engine.GetRenderer(), engine.GetDevice());
	DebugUI::DrawCameraPanel(engine.GetCamera(), cameraMoveSpeed);
	DebugUI::DrawEnginePanel(engine);
	ImGui::End();
}

void Editor::DrawStats(Engine& engine, const char* sceneName, const ModelStats& modelStats)
{
	ImGui::Begin("Stats");
	const Renderer::Stats& stats = engine.GetRenderer().GetStats();
	const Time& time = engine.GetTime();
	ImGui::Text("%.1f FPS   %.2f ms/frame   scene view %ux%u", time.GetFps(), time.GetAverageFrameMs(), m_sceneWidth, m_sceneHeight);
	ImGui::Text("draws %u (+%u shadow)   triangles %u   PSO switches %u   set switches %u   mesh switches %u   PSO cache %zu",
		stats.draws, stats.shadowDraws, stats.triangles, stats.pipelineSwitches, stats.resourceSetSwitches, stats.meshSwitches, engine.GetRenderer().GetPipelineCount());
	DebugUI::DrawModelPanel(sceneName, modelStats, engine.GetScene().GetImageCount(), engine.GetScene().GetObjects().size());
	DebugUI::DrawProfilerPanel();
	ImGui::End();
}

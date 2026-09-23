#include "pch.h"
#include "App/Editor.h"
#include "App/DebugUI.h"
#include "Core/Engine.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "Core/Paths.h"
#include "Graphics/Mesh.h"
#include "Graphics/Model.h"
#include "RHI/Device.h"
#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder (아직 내부 API)
#include <ImGuizmo.h>
#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	// DirectXMath 의 RotationRollPitchYaw(pitch, yaw, roll) = Rz(roll)·Rx(pitch)·Ry(yaw) (행벡터) 에서 각을 되찾는다.
	// 행렬 원소: m[2][1] = −sin p, m[2][0] = cos p·sin y, m[2][2] = cos p·cos y, m[0][1] = sin r·cos p, m[1][1] = cos r·cos p.
	XMFLOAT3 EulerFromRotation(const XMFLOAT4X4& m)
	{
		XMFLOAT3 euler;
		const float sp = -m.m[2][1];
		euler.x = asinf(std::clamp(sp, -1.0f, 1.0f));
		if (fabsf(sp) < 0.9999f)
		{
			euler.y = atan2f(m.m[2][0], m.m[2][2]);
			euler.z = atan2f(m.m[0][1], m.m[1][1]);
		}
		else
		{
			// 짐벌락: yaw 를 0 으로 두고 roll 에 몰아준다.
			euler.y = 0.0f;
			euler.z = atan2f(-m.m[1][0], m.m[0][0]);
		}
		return euler;
	}

	// 광선-AABB (slab). t ≥ 0 인 가장 가까운 교차. 없으면 false.
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

	DrawMenuBar(engine, callbacks);

	// 단축키: Ctrl+S 저장, Ctrl+L 로드, 1/2/3 기즈모 (텍스트 입력 중이 아닐 때)
	if (!ImGui::GetIO().WantTextInput)
	{
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) && callbacks.saveScene) m_lastMessage = callbacks.saveScene(scenePath) ? "saved" : "save failed";
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L) && callbacks.loadScene) { m_lastMessage = callbacks.loadScene(scenePath) ? "loaded" : "load failed"; m_selected = -1; }
		if (ImGui::IsKeyPressed(ImGuiKey_1, false)) m_gizmoOperation = 0;
		if (ImGui::IsKeyPressed(ImGuiKey_2, false)) m_gizmoOperation = 1;
		if (ImGui::IsKeyPressed(ImGuiKey_3, false)) m_gizmoOperation = 2;
	}
	DrawSceneView(engine);
	DrawHierarchy(engine, callbacks);
	DrawInspector(engine);
	DrawRenderSettings(engine);
	DrawStats(engine, sceneName, modelStats);

	ImGui::Begin("Lights");
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
	ImGui::DockBuilderFinish(dockspaceId);
}

void Editor::DrawMenuBar(Engine& engine, const Callbacks& callbacks)
{
	if (!ImGui::BeginMainMenuBar()) return;
	if (ImGui::BeginMenu("File"))
	{
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
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("View"))
	{
		ImGui::MenuItem("Console (F12)", nullptr, &showConsole);
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
		ImGui::Checkbox("World space", &m_gizmoWorld);
		ImGui::EndMenu();
	}
	ImGui::Text("   %s   |   backend %s   |   %s", m_lastMessage.c_str(), engine.GetDevice().GetBackendName(),
		m_selected >= 0 && m_selected < static_cast<int>(engine.GetScene().GetObjects().size()) ? engine.GetScene().GetObjects()[m_selected].GetName().c_str() : "(no selection)");
	ImGui::EndMainMenuBar();
}

void Editor::DrawSceneView(Engine& engine)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("Scene");
	ImGui::PopStyleVar();

	// 툴바 한 줄
	ImGui::SetCursorPos(ImVec2(6.0f, 4.0f + ImGui::GetCursorPosY()));
	ImGui::RadioButton("Move", &m_gizmoOperation, 0); ImGui::SameLine();
	ImGui::RadioButton("Rotate", &m_gizmoOperation, 1); ImGui::SameLine();
	ImGui::RadioButton("Scale", &m_gizmoOperation, 2); ImGui::SameLine();
	ImGui::Checkbox("World", &m_gizmoWorld); ImGui::SameLine();
	ImGui::TextDisabled("RMB drag: look   WASD/QE: move   LMB: select   1/2/3: gizmo");

	const ImVec2 available = ImGui::GetContentRegionAvail();
	const uint32_t width = static_cast<uint32_t>((std::max)(available.x, 8.0f));
	const uint32_t height = static_cast<uint32_t>((std::max)(available.y, 8.0f));
	m_sceneWidth = width;
	m_sceneHeight = height;
	Renderer& renderer = engine.GetRenderer();
	renderer.SetSceneTarget(width, height);   // 크기가 같으면 아무 일도 없다
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
	m_sceneHovered = ImGui::IsItemHovered();
	m_sceneFocused = ImGui::IsWindowFocused();

	// 기즈모 (선택된 오브젝트). 이미지 위에 그린다.
	DrawGizmo(engine, imagePos.x, imagePos.y, static_cast<float>(width), static_cast<float>(height));

	// 클릭 선택. 기즈모를 잡고 있거나 기즈모 위면 제외.
	if (m_sceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
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

	GameObject& object = scene.GetObjects()[m_selected];
	const Camera& camera = engine.GetCamera();

	// DirectXMath 행벡터 행렬과 ImGuizmo(열벡터, 열우선 메모리)는 메모리 배치가 같다 — 이동이 [12..14] 에 있는 것이 그 증거.
	XMFLOAT4X4 view, proj, world;
	XMStoreFloat4x4(&view, camera.GetViewMatrix());
	XMStoreFloat4x4(&proj, camera.GetProjectionMatrix());
	XMStoreFloat4x4(&world, object.GetTransform().GetWorldMatrix());

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(x, y, width, height);
	const ImGuizmo::OPERATION operation = m_gizmoOperation == 0 ? ImGuizmo::TRANSLATE : (m_gizmoOperation == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE);
	const ImGuizmo::MODE mode = (m_gizmoWorld && m_gizmoOperation != 2) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
	if (ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0], operation, mode, &world.m[0][0]))
	{
		// world = pre × S·R·T 이므로 S·R·T = pre⁻¹ × world. 분해해 Transform 에 되돌린다.
		Transform& t = object.GetTransform();
		XMMATRIX local = XMLoadFloat4x4(&world);
		if (t.HasPreTransform()) local = XMMatrixInverse(nullptr, t.GetPreTransform()) * local;
		XMVECTOR scale, rotation, translation;
		if (XMMatrixDecompose(&scale, &rotation, &translation, local))
		{
			XMFLOAT4X4 rot;
			XMStoreFloat4x4(&rot, XMMatrixRotationQuaternion(rotation));
			XMFLOAT3 s, p;
			XMStoreFloat3(&s, scale);
			XMStoreFloat3(&p, translation);
			t.SetScale(s);
			t.SetRotation(EulerFromRotation(rot));
			t.SetPosition(p);
		}
	}
	m_gizmoUsing = ImGuizmo::IsUsing();
}

int Editor::Pick(Scene& scene, const Camera& camera, float u, float v) const
{
	if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return -1;
	// 픽셀 → NDC → 월드 광선 (near 와 far 점을 역투영).
	const float ndcX = u * 2.0f - 1.0f;
	const float ndcY = 1.0f - v * 2.0f;
	const XMMATRIX invViewProj = XMMatrixInverse(nullptr, camera.GetViewMatrix() * camera.GetProjectionMatrix());
	const XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
	const XMVECTOR farPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);
	const XMVECTOR worldDirection = XMVector3Normalize(farPoint - nearPoint);

	int best = -1;
	float bestT = FLT_MAX;
	const std::vector<GameObject>& objects = scene.GetObjects();
	for (int i = 0; i < static_cast<int>(objects.size()); ++i)
	{
		const Mesh* mesh = objects[i].GetMesh();
		if (mesh == nullptr || !mesh->IsValid()) continue;
		// 광선을 오브젝트 로컬 공간으로 옮겨 로컬 AABB 와 검사한다 (회전·스케일이 있어도 정확).
		const XMMATRIX world = objects[i].GetTransform().GetWorldMatrix();
		const XMMATRIX invWorld = XMMatrixInverse(nullptr, world);
		XMFLOAT3 origin, direction;
		XMStoreFloat3(&origin, XMVector3TransformCoord(nearPoint, invWorld));
		XMStoreFloat3(&direction, XMVector3TransformNormal(worldDirection, invWorld));
		const float length = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
		if (length < 1e-8f) continue;
		float tLocal = 0.0f;
		if (!RayAabb(origin, direction, mesh->GetBounds().min, mesh->GetBounds().max, tLocal)) continue;
		// 로컬 t 는 스케일된 방향 기준이라 월드 거리로 바꿔 비교한다.
		const float tWorld = tLocal * length;
		if (tWorld < bestT)
		{
			bestT = tWorld;
			best = i;
		}
	}
	return best;
}

void Editor::DrawHierarchy(Engine& engine, const Callbacks& callbacks)
{
	ImGui::Begin("Hierarchy");
	Scene& scene = engine.GetScene();
	std::vector<GameObject>& objects = scene.GetObjects();
	ImGui::Text("Objects %zu   Lights %zu", objects.size(), scene.GetLights().size());
	// 추가/삭제. 새 오브젝트는 원점 위에 기본 재질로 생기고 바로 선택된다.
	if (ImGui::SmallButton("+ Sphere") && callbacks.addPrimitive) { callbacks.addPrimitive(0); m_selected = static_cast<int>(objects.size()) - 1; }
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Cube") && callbacks.addPrimitive) { callbacks.addPrimitive(1); m_selected = static_cast<int>(objects.size()) - 1; }
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Cylinder") && callbacks.addPrimitive) { callbacks.addPrimitive(2); m_selected = static_cast<int>(objects.size()) - 1; }
	ImGui::SameLine();
	if (ImGui::SmallButton("+ Plane") && callbacks.addPrimitive) { callbacks.addPrimitive(3); m_selected = static_cast<int>(objects.size()) - 1; }
	const bool canDelete = m_selected >= 0 && m_selected < static_cast<int>(objects.size());
	const bool deleteKey = canDelete && (m_sceneFocused || ImGui::IsWindowFocused()) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false);
	if (canDelete) { ImGui::SameLine(); }
	if (canDelete && (ImGui::SmallButton("Delete (Del)") || deleteKey) && callbacks.deleteObject) { callbacks.deleteObject(m_selected); m_selected = -1; }
	ImGui::Separator();
	for (int i = 0; i < static_cast<int>(objects.size()); ++i)
	{
		ImGui::PushID(i);
		const bool selected = m_selected == i;
		std::string label = objects[i].GetName().empty() ? "(unnamed)" : objects[i].GetName();
		if (ImGui::Selectable(label.c_str(), selected)) m_selected = i;
		ImGui::PopID();
	}
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) m_selected = -1;
	ImGui::End();
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
	GameObject& object = scene.GetObjects()[m_selected];
	ImGui::Text("%s", object.GetName().c_str());
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

	// 재질: 씬의 재질 중 고르고, 그 자리에서 편집한다.
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
		ImGui::ColorEdit3("Specular", &material.specularColor.x);
		ImGui::SliderFloat("Shininess", &material.shininess, 1.0f, 256.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
		ImGui::DragFloat2("UV scale", &material.uvScale.x, 0.1f, 0.1f, 64.0f);
		ImGui::SliderFloat("Normal strength", &material.normalStrength, 0.0f, 3.0f);
		ImGui::SliderFloat("Alpha cutoff", &material.alphaCutoff, 0.0f, 1.0f);
		ImGui::Checkbox("Unlit", &material.unlit); ImGui::SameLine();
		ImGui::Checkbox("Wireframe", &material.wireframe); ImGui::SameLine();
		ImGui::Checkbox("Double sided", &material.doubleSided);
		ImGui::TextDisabled("albedo: %s   normal: %s", material.albedoTexture.empty() ? "(none)" : material.albedoTexture.c_str(),
			material.normalTexture.empty() ? "(none)" : material.normalTexture.c_str());
	}
	ImGui::End();
}

void Editor::DrawRenderSettings(Engine& engine)
{
	ImGui::Begin("Render Settings");
	DebugUI::DrawRenderSettingsPanel(engine.GetRenderer(), engine.GetDevice());
	RenderSettings& settings = engine.GetRenderer().GetSettings();
	const char* views[] = { "Lit", "Albedo", "World normal", "Depth", "Shadow factor", "UV" };
	ImGui::Combo("Debug view", &settings.debugView, views, 6);
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

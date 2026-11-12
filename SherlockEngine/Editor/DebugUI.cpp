#include "pch.h"
#include "Editor/DebugUI.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "Graphics/Model.h"   // ModelStats
#include "Graphics/TextureImporter.h"
#include "Scene/Material.h"
#include "Core/Engine.h"
#include "Core/Config.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "RHI/RHI.h"
#include "Editor/ContentBrowser.h"   // 스카이박스 칸의 드롭 페이로드
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <cfloat>
#include <map>
#include <vector>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	const char* kSamplerNames[] = { "LinearWrap", "LinearClamp", "AnisotropicWrap", "PointWrap", "PointNoMip" };
	const char* kLightTypeNames[] = { "Directional", "Point", "Spot" };

	// R 버튼의 기본값. 구조체의 멤버 초기값을 그대로 읽으므로 기본값을 바꿔도 이 파일은 고칠 필요가 없음.
	const ShadowSettings kShadowDefaults;
	const SceneEnvironment kEnvironmentDefaults;
	const Light kLightDefaults;
	const Material kMaterialDefaults;
	constexpr float kCameraMoveSpeedDefault = 10.0f;   // EditorApp::m_moveSpeed 의 초기값
	constexpr float kCameraOrthoHeightDefault = 25.0f; // Camera 생성자의 m_orthoHeight
}

bool DebugUI::BeginPanel(const char* name, bool* open)
{
	ImGui::SetNextWindowSizeConstraints(ImVec2(180.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));   // 떠 있는 창일 때만 의미가 있음 (도킹 노드는 도킹 쪽이 정함)
	const bool visible = ImGui::Begin(name, open);
	const float available = ImGui::GetContentRegionAvail().x;
	const float labelWidth = std::clamp(available * 0.42f, 110.0f, 170.0f);   // 오른쪽 라벨에 남겨 둘 폭
	ImGui::PushItemWidth((std::max)(available - labelWidth - ImGui::GetStyle().ItemInnerSpacing.x, 90.0f));
	return visible;
}

void DebugUI::EndPanel()
{
	ImGui::PopItemWidth();
	ImGui::End();
}

void DebugUI::SameLineIfFits(float itemWidth)
{
	const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;   // 줄 시작 x + 남은 폭 = 오른쪽 끝
	if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + itemWidth <= right) ImGui::SameLine();
}

float DebugUI::CheckboxWidth(const char* label)
{
	return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label, nullptr, true).x;
}

float DebugUI::ButtonWidth(const char* label)
{
	return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

float DebugUI::TextItemWidth(const char* text)
{
	return ImGui::CalcTextSize(text, nullptr, true).x;
}

namespace
{
	// 위젯을 그리기 전에 부름: 위젯 폭에서 R 버튼과 간격만큼을 뺌 (라벨은 위젯 오른쪽에 그려지므로 라벨 + R
	// 이 원래 라벨 자리에 들어감).
	void ReserveResetButton()
	{
		ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - DebugUI::ButtonWidth("R") - ImGui::GetStyle().ItemSpacing.x);
	}

	// 위젯 바로 뒤(라벨 옆)에 R 버튼을 그림. 기본값과 같으면 비활성. 눌리면 true.
	bool DrawResetButton(const char* id, bool atDefault, const char* defaultText)
	{
		ImGui::SameLine();
		ImGui::PushID(id);
		ImGui::BeginDisabled(atDefault);
		const bool pressed = ImGui::SmallButton("R");
		ImGui::EndDisabled();
		ImGui::PopID();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Reset to default: %s", defaultText);
		return pressed;
	}
}

bool DebugUI::SliderFloatR(const char* label, float* value, float defaultValue, float min, float max, const char* format, int flags)
{
	ReserveResetButton();
	bool changed = ImGui::SliderFloat(label, value, min, max, format, flags);
	char text[32];
	snprintf(text, sizeof(text), "%g", defaultValue);
	if (DrawResetButton(label, *value == defaultValue, text)) { *value = defaultValue; changed = true; }
	return changed;
}

bool DebugUI::DragFloatR(const char* label, float* value, float defaultValue, float speed, float min, float max, const char* format)
{
	ReserveResetButton();
	bool changed = ImGui::DragFloat(label, value, speed, min, max, format);
	char text[32];
	snprintf(text, sizeof(text), "%g", defaultValue);
	if (DrawResetButton(label, *value == defaultValue, text)) { *value = defaultValue; changed = true; }
	return changed;
}

bool DebugUI::DragFloat2R(const char* label, float* value, float defaultX, float defaultY, float speed, float min, float max, const char* format)
{
	ReserveResetButton();
	bool changed = ImGui::DragFloat2(label, value, speed, min, max, format);
	char text[64];
	snprintf(text, sizeof(text), "%g, %g", defaultX, defaultY);
	if (DrawResetButton(label, value[0] == defaultX && value[1] == defaultY, text)) { value[0] = defaultX; value[1] = defaultY; changed = true; }
	return changed;
}

bool DebugUI::DragFloat3R(const char* label, float* value, float defaultX, float defaultY, float defaultZ, float speed, float min, float max, const char* format)
{
	ReserveResetButton();
	bool changed = ImGui::DragFloat3(label, value, speed, min, max, format);
	char text[96];
	snprintf(text, sizeof(text), "%g, %g, %g", defaultX, defaultY, defaultZ);
	if (DrawResetButton(label, value[0] == defaultX && value[1] == defaultY && value[2] == defaultZ, text))
	{
		value[0] = defaultX; value[1] = defaultY; value[2] = defaultZ;
		changed = true;
	}
	return changed;
}

void DebugUI::DrawCameraPanel(Camera& camera, float& moveSpeed)
{
	ImGui::Separator();
	ImGui::TextWrapped("Camera  (WASD/QE move, RMB drag look, Shift fast)");
	const XMFLOAT3& p = camera.GetPosition();
	ImGui::Text("pos   %.2f  %.2f  %.2f", p.x, p.y, p.z);
	ImGui::TextWrapped("yaw   %.1f deg   pitch %.1f deg   roll %.1f deg",
		XMConvertToDegrees(camera.GetYaw()), XMConvertToDegrees(camera.GetPitch()), XMConvertToDegrees(camera.GetRoll()));   // roll 은 재생 중 부모가 기울어진 카메라에서만 0 이 아님 (11-F)
	SliderFloatR("Speed", &moveSpeed, kCameraMoveSpeedDefault, 1.0f, 50.0f);
	// 에디터 카메라의 투영 선택. 재생 중 카메라 컴포넌트가 카메라를 움직이면 그 컴포넌트의 설정이 덮어씀.
	bool orthographic = !camera.IsUsePerspectiveProjection();
	if (ImGui::Checkbox("Orthographic", &orthographic)) camera.SetUsePerspectiveProjection(!orthographic);
	if (orthographic)
	{
		float height = camera.GetOrthoHeight();
		if (SliderFloatR("Ortho height", &height, kCameraOrthoHeightDefault, 1.0f, 100.0f)) camera.SetOrthoHeight(height);
	}
	ImGui::TextWrapped("F1 wire  F2 cull  F3 vsync  F4 reload  F5 sRGB  F6 sampler  F7 light orbit  F8 shadows  F9 normal maps  F10 pause");
}

void DebugUI::DrawRenderSettingsPanel(Renderer& renderer, RHI::Device& device)
{
	ImGui::Separator();
	ImGui::TextWrapped("Pipeline   backend: %s (--backend=d3d11|d3d12)", device.GetBackendName());
	RenderSettings& settings = renderer.GetSettings();
	ImGui::Checkbox("Wireframe", &settings.wireframe);
	SameLineIfFits(CheckboxWidth("Cull back faces"));
	ImGui::Checkbox("Cull back faces", &settings.cullBack);
	SameLineIfFits(CheckboxWidth("sRGB output"));
	ImGui::Checkbox("sRGB output", &settings.srgbOutput);
	SameLineIfFits(CheckboxWidth("Normal maps"));
	ImGui::Checkbox("Normal maps", &settings.normalMapping);
	bool vsync = device.IsVSync();
	if (ImGui::Checkbox("VSync", &vsync))
	{
		device.SetVSync(vsync);
	}
	const char* tearing = device.IsTearingSupported() ? "(tearing OK)" : "(no tearing)";
	SameLineIfFits(TextItemWidth(tearing));
	ImGui::TextUnformatted(tearing);
	SameLineIfFits(ButtonWidth("Reload shaders"));
	if (ImGui::Button("Reload shaders"))
	{
		renderer.ReloadShaders();
	}

	// -1 = 재질 설정을 따름. 콤보의 첫 항목이 이 값에 해당함.
	int samplerIndex = settings.samplerOverride + 1;
	const char* samplerItems[] = { "(per material)", "LinearWrap", "LinearClamp", "AnisotropicWrap", "PointWrap", "PointNoMip" };
	if (ImGui::Combo("Sampler override", &samplerIndex, samplerItems, ARRAYSIZE(samplerItems)))
	{
		settings.samplerOverride = samplerIndex - 1;
	}

	const Renderer::Stats& stats = renderer.GetStats();
	ImGui::TextWrapped("draws %u (+%u shadow, %u views, culled %u)   PSO switches %u   set switches %u   mesh switches %u",
		stats.draws, stats.shadowDraws, stats.shadowViews, stats.shadowCulled, stats.pipelineSwitches, stats.resourceSetSwitches, stats.meshSwitches);
	ImGui::TextWrapped("passes %u   barriers %u   PSO cache %zu   GPU meshes %zu   GPU materials %zu   textures %zu   reloads %u   frame slot %u",
		stats.renderPasses, stats.barriers, renderer.GetPipelineCount(), renderer.GetGpuMeshCount(), renderer.GetGpuMaterialCount(),
		renderer.GetTextureCount(), stats.shaderReloads, device.GetFrameIndex());
}

void DebugUI::DrawShadowPanel(Scene& scene, Renderer& renderer, RHI::Device& device)
{
	ImGui::Separator();
	RenderSettings& settings = renderer.GetSettings();
	ImGui::Checkbox("Shadows (F8)", &settings.shadows);
	// 아래 값들은 씬 파일에 저장됨 (scene.environment.shadow). 켜고 끄는 스위치만 렌더러 설정.
	ShadowSettings& shadow = scene.environment.shadow;
	SliderFloatR("Shadow bias", &shadow.bias, kShadowDefaults.bias, 0.0f, 0.01f, "%.4f");
	SliderFloatR("Shadow strength", &shadow.strength, kShadowDefaults.strength, 0.0f, 1.0f);
	SliderFloatR("Normal offset", &shadow.normalOffset, kShadowDefaults.normalOffset, 0.0f, 1.0f, "%.3f");
	SliderFloatR("Ortho size", &shadow.orthoSize, kShadowDefaults.orthoSize, 10.0f, 150.0f);
	if (shadow.cascadeEnabled) ImGui::TextDisabled("  (CSM on: Ortho size only scales the bias)");
	SliderFloatR("Light distance", &shadow.distance, kShadowDefaults.distance, 5.0f, 120.0f);
	SliderFloatR("Local normal bias (texels)", &shadow.localNormalBias, kShadowDefaults.localNormalBias, 0.0f, 4.0f);
	ImGui::Checkbox("CSM (off = single directional view)", &shadow.cascadeEnabled);
	if (shadow.cascadeEnabled)
	{
		ImGui::SliderInt("Cascades", &shadow.cascadeCount, 2, 4);
		SliderFloatR("Cascade distance", &shadow.cascadeDistance, kShadowDefaults.cascadeDistance, 5.0f, 300.0f);
		SliderFloatR("Cascade lambda", &shadow.cascadeLambda, kShadowDefaults.cascadeLambda, 0.0f, 1.0f);
		SliderFloatR("Cascade blend", &shadow.cascadeBlend, kShadowDefaults.cascadeBlend, 0.0f, 0.5f);
	}
	if (ImGui::TreeNode("Shadow atlas (2048x2048 D32, local lights, shown as R)"))
	{
		// R32_FLOAT SRV 를 ImGui 가 RGBA 로 샘플하면 빨강 채널에 깊이가 담김. 가까울수록 검고 멀수록 붉게 보임.
		const ImTextureID id = static_cast<ImTextureID>(device.GetImGuiTextureId(renderer.GetShadowMap()));
		if (id != 0)
		{
			ImGui::Image(id, ImVec2(512.0f, 512.0f));
			// 이번 프레임에 그린 뷰의 사각형과 조명 번호를 노란 테두리로 겹쳐 그림
			const ImVec2 origin = ImGui::GetItemRectMin();
			const float scale = 512.0f / Renderer::kShadowAtlasSize;
			ImDrawList* draw = ImGui::GetWindowDrawList();
			for (uint32_t v = 0; v < renderer.GetShadowViewCount(); ++v)
			{
				const Renderer::ShadowView& view = renderer.GetShadowView(v);
				if (view.atlas != 0) continue;   // CSM 아틀라스 소속 뷰는 아래 트리에서 보여 줌
				const ImVec2 a(origin.x + view.x * scale, origin.y + view.y * scale);
				const ImVec2 b(a.x + view.size * scale, a.y + view.size * scale);
				draw->AddRect(a, b, IM_COL32(255, 220, 0, 255));
				char label[16];
				snprintf(label, sizeof(label), "L%u", view.lightIndex);
				draw->AddText(ImVec2(a.x + 3, a.y + 2), IM_COL32(255, 220, 0, 255), label);
			}
		}
		ImGui::Text("views %u / %u   local tiles %u / %u   skipped lights %u",
			renderer.GetShadowViewCount(), MAX_SHADOW_VIEWS, renderer.GetUsedLocalTiles(), Renderer::kShadowLocalTileCount,
			renderer.GetSkippedShadowLights());
		ImGui::TreePop();
	}
	// 방향광 CSM 전용 아틀라스. 캐스케이드마다 해상도가 다름 (C0 이 가장 크고 가까운 구간). CSM 이 꺼져
	// 있으면 마지막 CSM 프레임의 깊이가 남아 있거나 비어 있음.
	if (ImGui::TreeNode("CSM atlas (2048x2048 D32, directional, shown as R)"))
	{
		const ImTextureID csmId = static_cast<ImTextureID>(device.GetImGuiTextureId(renderer.GetCsmMap()));
		if (csmId != 0)
		{
			const float shownWidth = 512.0f;
			const float scale = shownWidth / Renderer::kCsmAtlasWidth;
			ImGui::Image(csmId, ImVec2(shownWidth, Renderer::kCsmAtlasHeight * scale));
			const ImVec2 origin = ImGui::GetItemRectMin();
			ImDrawList* draw = ImGui::GetWindowDrawList();
			// 색은 씬의 캐스케이드 디버그 색(빨강·초록·파랑·노랑)과 같음. 캐스케이드 번호 = 이 아틀라스 뷰의 등장 순서.
			static const ImU32 kCascadeColor[4] = { IM_COL32(255, 64, 64, 255), IM_COL32(64, 255, 64, 255),
				IM_COL32(96, 128, 255, 255), IM_COL32(255, 220, 0, 255) };
			uint32_t cascade = 0;
			for (uint32_t v = 0; v < renderer.GetShadowViewCount() && cascade < 4; ++v)
			{
				const Renderer::ShadowView& view = renderer.GetShadowView(v);
				if (view.atlas != 1) continue;
				const ImVec2 a(origin.x + view.x * scale, origin.y + view.y * scale);
				const ImVec2 b(a.x + view.size * scale, a.y + view.size * scale);
				draw->AddRect(a, b, kCascadeColor[cascade], 0.0f, 0, 2.0f);
				char label[24];
				snprintf(label, sizeof(label), "C%u %u", cascade, view.size);
				draw->AddText(ImVec2(a.x + 3, a.y + 2), kCascadeColor[cascade], label);
				++cascade;
			}
		}
		ImGui::Text("cascades %u", renderer.GetCascadeFrustumCount());
		ImGui::TreePop();
	}
}

namespace
{
	// 환경 칸 하나 — 이름 입력(Enter 로 적용) + 콘텐츠 브라우저의 텍스처 드롭.
	// 규칙은 재질 텍스처와 같음: Textures\ 기준 상대 경로 또는 "asset:<Assets 기준>".
	// cube 가 참이면 .dds 는 그 파일(큐브 DDS), 그 밖의 이미지는 그 이미지가 든 폴더(면 6장)를 씀 — 폴더
	// 타일은 드래그할 수 없기 때문. cube 가 거짓(BRDF LUT)이면 떨어뜨린 파일 그대로.
	void EditEnvironmentName(const char* label, const char* hint, std::string& value, bool cube)
	{
		char buffer[260];
		strncpy_s(buffer, value.c_str(), _TRUNCATE);
		if (ImGui::InputTextWithHint(label, hint, buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
		{
			value = buffer;
		}
		if (!ImGui::BeginDragDropTarget()) return;
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayloadType);
		if (payload != nullptr && payload->DataSize == sizeof(AssetDragPayload))
		{
			AssetDragPayload asset = *static_cast<const AssetDragPayload*>(payload->Data);
			asset.relativePath[259] = L'\0';   // 종료 문자 방어
			std::wstring relative = asset.relativePath;
			if (asset.type == AssetType::Texture)
			{
				const size_t dot = relative.find_last_of(L'.');
				const bool dds = dot != std::wstring::npos && _wcsicmp(relative.c_str() + dot, L".dds") == 0;
				if (cube && !dds)
				{
					const size_t slash = relative.find_last_of(L'\\');
					relative = slash == std::wstring::npos ? std::wstring() : relative.substr(0, slash);
				}
				// Textures\ 바로 아래 항목은 이름만, 그 밖은 "asset:" 접두사 (ContentBrowser::ToMaterialTextureName).
				if (!relative.empty()) value = ContentBrowser::ToMaterialTextureName(relative);
				Log::Info("환경 드롭 (%s): %s", label, value.c_str());
			}
		}
		ImGui::EndDragDropTarget();
	}
}

namespace
{
	bool g_showPostStages = false;   // 에디터 표시 설정 (씬에 저장하지 않음)
}

void DebugUI::SetShowPostStages(bool show)
{
	g_showPostStages = show;
}

void DebugUI::DrawPostStagesWindow(Renderer& renderer, RHI::Device& device)
{
	if (!g_showPostStages) return;
	renderer.RequestPostCapture();   // 다음 프레임의 Render 가 단계별 썸네일을 그림. 창을 닫으면 요청이 끊겨 비용이 사라짐

	// 처음에는 씬 뷰 왼쪽 위에 떠 있고, 도킹해도 됨.
	ImGui::SetNextWindowPos(ImVec2(250.0f, 80.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(1080.0f, 285.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Post Stages", &g_showPostStages))
	{
		ImGui::SetWindowFontScale(1.5f);   // 글씨 기본 1.5배
		ImGui::TextDisabled("HDR 결과를 톤매핑해 본 모습. Scene/Blur/Bloom 은 색 효과(Greyscale) 전, Final 은 후처리 + 색 효과 후.");
		const uint32_t sourceW = renderer.GetPostStageWidth();
		const uint32_t sourceH = renderer.GetPostStageHeight();
		if (sourceW == 0)
		{
			ImGui::TextUnformatted("waiting for the first captured frame...");
		}
		else
		{
			// 창 폭에 맞춰 4칸을 같은 크기로 나눔 (칸 사이 여백 제외). 창을 키우면 이미지도 같이 커짐
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			const float cell = (std::max)(16.0f, (ImGui::GetContentRegionAvail().x - spacing * (Renderer::kPostStageCount - 1)) / Renderer::kPostStageCount);
			const ImVec2 size(cell, cell * static_cast<float>(sourceH) / static_cast<float>(sourceW));
			for (uint32_t i = 0; i < Renderer::kPostStageCount; ++i)
			{
				const Renderer::PostStageThumb& stage = renderer.GetPostStage(i);
				ImGui::BeginGroup();
				ImGui::TextUnformatted(stage.name);
				const uint64_t id = stage.active ? device.GetImGuiTextureId(stage.texture) : 0;
				if (id != 0) ImGui::Image(static_cast<ImTextureID>(id), size);
				else ImGui::Dummy(size);   // 꺼진 효과 (Blur/Bloom): 자리만 비움
				if (!stage.active) ImGui::TextDisabled("off");
				ImGui::EndGroup();
				if (i + 1 < Renderer::kPostStageCount) ImGui::SameLine();
			}
		}
	}
	ImGui::End();
}

void DebugUI::DrawEnvironmentPanel(Scene& scene)
{
	// 이름만 바꾸면 됨 — Renderer 가 다음 프레임에 이름으로 큐브·LUT 를 읽어(캐시) 프레임 셋 t9~t12 에 꽂음.
	// Scene 은 GPU 를 모름.
	SceneEnvironment& env = scene.environment;
	static const char* const kPresets[] = { "(none)", "builtin:sky", "builtin:skyfaces" };
	const char* preview = env.skybox.empty() ? kPresets[0] : env.skybox.c_str();
	if (ImGui::BeginCombo("Skybox", preview))
	{
		for (int i = 0; i < IM_ARRAYSIZE(kPresets); ++i)
		{
			const std::string value = i == 0 ? std::string() : kPresets[i];
			if (ImGui::Selectable(kPresets[i], env.skybox == value)) env.skybox = value;
		}
		ImGui::EndCombo();
	}
	EditEnvironmentName("Skybox file", "asset:Cubemap\\Sky.dds  or  Skybox (folder of 6 faces)", env.skybox, true);

	// IBL. 비워 두면 스카이박스를 밉으로 흐려 대신 쓰고(근사), 스카이박스도 없으면 ambientColor.
	if (ImGui::TreeNodeEx("Image based lighting", ImGuiTreeNodeFlags_DefaultOpen))
	{
		EditEnvironmentName("Irradiance", "diffuse cube (.dds)", env.irradiance, true);
		EditEnvironmentName("Specular", "prefiltered cube, mip = roughness (.dds)", env.specular, true);
		EditEnvironmentName("BRDF LUT", "2D, u = N.V, v = 1 - roughness", env.brdfLut, false);
		SliderFloatR("IBL intensity", &env.iblIntensity, kEnvironmentDefaults.iblIntensity, 0.0f, 4.0f);
		ImGui::TextDisabled(env.irradiance.empty() && env.specular.empty()
			? (env.skybox.empty() ? "no environment: ambient color is used" : "no IBL maps: blurred skybox mips are used (approximation)")
			: "IBL maps in use");
		ImGui::TreePop();
	}
	SliderFloatR("Exposure", &env.exposure, kEnvironmentDefaults.exposure, 0.05f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
	SameLineIfFits(CheckboxWidth("ACES tone mapping"));
	ImGui::Checkbox("ACES tone mapping", &env.toneMapping);

	// 후처리. 순서는 Renderer 가 고정함 (HDR 구간: 블러 → 블룸, 톤 패스: 노출 → ACES → 색 효과).
	// 환경의 일부라 씬 JSON 과 되돌리기 이력에 함께 들어감.
	if (ImGui::TreeNodeEx("Post process", ImGuiTreeNodeFlags_DefaultOpen))
	{
		PostProcessSettings& post = env.post;
		ImGui::Checkbox("Gaussian blur", &post.blurEnabled);
		if (post.blurEnabled) SliderFloatR("Blur radius (px @720p)", &post.blurRadius, kEnvironmentDefaults.post.blurRadius, 0.5f, 32.0f, "%.1f");
		ImGui::Checkbox("Bloom", &post.bloomEnabled);
		if (post.bloomEnabled)
		{
			SliderFloatR("Bloom threshold", &post.bloomThreshold, kEnvironmentDefaults.post.bloomThreshold, 0.0f, 8.0f, "%.2f");
			SliderFloatR("Bloom knee", &post.bloomKnee, kEnvironmentDefaults.post.bloomKnee, 0.0f, 1.0f, "%.2f");
			SliderFloatR("Bloom intensity", &post.bloomIntensity, kEnvironmentDefaults.post.bloomIntensity, 0.0f, 4.0f, "%.2f");
		}
		SliderFloatR("Greyscale", &post.greyscale, kEnvironmentDefaults.post.greyscale, 0.0f, 1.0f);
		ImGui::Checkbox("Stage thumbnails", &g_showPostStages);   // 씬이 아니라 에디터 설정 — 되돌리기 이력에 들어가지 않음
		ImGui::TreePop();
	}
}

void DebugUI::DrawLightPanel(Scene& scene, int* selectedLight, bool* openSelected)
{
	ImGui::Separator();
	std::vector<Light>& lights = scene.GetLights();
	ImGui::TextWrapped("Lights (%zu)   shadows: first directional (cascades) + spot (1 tile) + point (6 tiles), %u local tiles", lights.size(), Renderer::kShadowLocalTileCount);
	ImGui::ColorEdit3("Ambient", &scene.ambientColor.x);
	{
		SameLineIfFits(ButtonWidth("Add point light"));
		if (ImGui::Button("Add point light"))
		{
			Light light;
			light.type = LightType::Point;
			light.position = XMFLOAT3(0.0f, 6.0f, 0.0f);
			light.range = 20.0f;
			light.intensity = 2.0f;
			light.castShadows = false;   // 지역광 그림자는 타일을 쓰므로 기본은 꺼 둠 (Cast shadows 체크박스로 켬)
			lights.push_back(light);
		}
	}

	int removeIndex = -1;
	for (int i = 0; i < static_cast<int>(lights.size()); ++i)
	{
		Light& light = lights[i];
		ImGui::PushID(i);
		const int typeIndex = static_cast<int>(light.type) < 3 ? static_cast<int>(light.type) : 0;
		const bool isSelected = selectedLight != nullptr && *selectedLight == i;
		if (isSelected && openSelected != nullptr && *openSelected)
		{
			ImGui::SetNextItemOpen(true);   // 씬 뷰에서 조명을 클릭해 고른 직후: 해당 노드를 펼침
			ImGui::SetScrollHereY();
			*openSelected = false;
		}
		const bool open = ImGui::TreeNodeEx("light", isSelected ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None, "Light %d  %s", i, kLightTypeNames[typeIndex]);
		if (selectedLight != nullptr && ImGui::IsItemClicked()) *selectedLight = i;   // 패널에서 눌러도 씬 뷰 기즈모가 붙음
		if (open)
		{
			int selectedType = typeIndex;
			if (ImGui::Combo("Type", &selectedType, kLightTypeNames, ARRAYSIZE(kLightTypeNames)))
			{
				light.type = static_cast<LightType>(selectedType);
			}
			if (light.type != LightType::Directional)
			{
				DragFloat3R("Position", &light.position.x, kLightDefaults.position.x, kLightDefaults.position.y, kLightDefaults.position.z, 0.1f, -60.0f, 60.0f);
				DragFloatR("Range", &light.range, kLightDefaults.range, 0.1f, 0.1f, 200.0f);
			}
			if (light.type != LightType::Point)
			{
				DragFloat3R("Direction", &light.direction.x, kLightDefaults.direction.x, kLightDefaults.direction.y, kLightDefaults.direction.z, 0.01f, -1.0f, 1.0f);
				SameLineIfFits(ButtonWidth("normalize"));
				if (ImGui::SmallButton("normalize"))
				{
					XMVECTOR d = XMLoadFloat3(&light.direction);
					if (XMVectorGetX(XMVector3LengthSq(d)) > 0.000001f) XMStoreFloat3(&light.direction, XMVector3Normalize(d));
				}
			}
			ImGui::ColorEdit3("Color", &light.color.x);
			SliderFloatR("Intensity", &light.intensity, kLightDefaults.intensity, 0.0f, 10.0f);

			ImGui::Checkbox("Cast shadows", &light.castShadows);

			if (light.type == LightType::Spot)
			{
				float inner = XMConvertToDegrees(acosf(light.innerConeCos));
				float outer = XMConvertToDegrees(acosf(light.outerConeCos));
				// 기본값도 슬라이더와 같은 식(acos → 도)으로 구해야 기본 상태에서 R 이 정확히 비활성이 됨.
				if (SliderFloatR("Inner cone (deg)", &inner, XMConvertToDegrees(acosf(kLightDefaults.innerConeCos)), 1.0f, 89.0f)) light.innerConeCos = cosf(XMConvertToRadians(inner));
				if (SliderFloatR("Outer cone (deg)", &outer, XMConvertToDegrees(acosf(kLightDefaults.outerConeCos)), 1.0f, 89.0f)) light.outerConeCos = cosf(XMConvertToRadians(outer));
				if (light.outerConeCos > light.innerConeCos) light.outerConeCos = light.innerConeCos;
			}
			if (light.type != LightType::Directional)
			{
				DragFloat3R("Attenuation (c, l, q)", &light.attenuation.x, kLightDefaults.attenuation.x, kLightDefaults.attenuation.y, kLightDefaults.attenuation.z, 0.001f, 0.0f, 10.0f, "%.4f");
			}
			if (lights.size() > 1 && ImGui::Button("Remove"))
			{
				removeIndex = i;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (removeIndex >= 0)
	{
		lights.erase(lights.begin() + removeIndex);
		if (selectedLight != nullptr)
		{
			if (*selectedLight == removeIndex) *selectedLight = -1;
			else if (*selectedLight > removeIndex) --*selectedLight;
		}
	}
}

void DebugUI::DrawTextureImportSettings(const Material& material, Renderer& renderer)
{
	struct Slot { const char* label; const std::string* name; bool srgb; };
	const Slot slots[] = {
		{ "Albedo", &material.albedoTexture, true }, { "Normal", &material.normalTexture, false },
		{ "Metal/Rough", &material.metallicRoughnessTexture, false }, { "AO", &material.occlusionTexture, false },
		{ "Emissive", &material.emissiveTexture, true },
	};
	static std::map<std::string, TextureImportSettings> pending;   // 아직 Apply 하지 않은 편집값 (이름별)
	static const char* kSizes[] = { "Original", "4096", "2048", "1024", "512", "256", "128" };
	static const uint32_t kSizeValues[] = { 0, 4096, 2048, 1024, 512, 256, 128 };
	static const char* kMips[] = { "All", "None", "Top N" };

	bool any = false;
	for (const Slot& slot : slots)
	{
		const std::string& name = *slot.name;
		if (name.empty() || name.rfind("builtin:", 0) == 0) continue;
		if (!any) { ImGui::SeparatorText("Texture import"); any = true; }
		ImGui::PushID(slot.label);
		if (ImGui::TreeNode("slot", "%s: %s", slot.label, name.c_str()))
		{
			if (name.size() > 4 && _stricmp(name.c_str() + name.size() - 4, ".dds") == 0)
			{
				ImGui::TextDisabled("DDS source - used as is (import settings do not apply)");
			}
			else
			{
				const TextureImportSettings saved = TextureImporter::GetSettings(name);
				TextureImportSettings& edit = pending.try_emplace(name, saved).first->second;
				ImGui::Checkbox("BC7 compress", &edit.compress);
				int mipMode = edit.mipLevels == 0 ? 0 : (edit.mipLevels == 1 ? 1 : 2);
				if (ImGui::Combo("Mips", &mipMode, kMips, IM_ARRAYSIZE(kMips)))
					edit.mipLevels = mipMode == 0 ? 0 : (mipMode == 1 ? 1 : (std::max)(edit.mipLevels, 6u));
				if (mipMode == 2)
				{
					int levels = static_cast<int>(edit.mipLevels);
					if (ImGui::SliderInt("Levels", &levels, 2, 13)) edit.mipLevels = static_cast<uint32_t>(levels);
				}
				int sizeIndex = 0;
				for (int i = 0; i < IM_ARRAYSIZE(kSizeValues); ++i) if (kSizeValues[i] == edit.maxSize) sizeIndex = i;
				if (ImGui::Combo("Max size", &sizeIndex, kSizes, IM_ARRAYSIZE(kSizes))) edit.maxSize = kSizeValues[sizeIndex];
				const bool changed = !(edit == saved);
				ImGui::BeginDisabled(!changed);
				if (ImGui::Button("Apply"))
				{
					TextureImporter::SetSettings(name, edit);
					renderer.ReloadTexture(name);   // 다음 프레임에 새 설정으로 변환·로드 (큰 텍스처는 몇 초 걸림)
				}
				ImGui::SameLine();
				if (ImGui::Button("Revert")) edit = saved;
				ImGui::EndDisabled();
				if (changed) { ImGui::SameLine(); ImGui::TextDisabled("modified"); }
				if (!TextureImporter::IsBuildEnabled()) ImGui::TextDisabled("conversion runs in the editor only");
				ImGui::TextDisabled("%s", slot.srgb ? "color (sRGB)" : "data (linear)");
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void DebugUI::DrawMaterialPanel(Scene& scene, Renderer& renderer)
{
	ImGui::Separator();
	ImGui::Text("Materials (%zu)", scene.GetMaterials().size());
	int index = 0;
	for (auto& materialPtr : scene.GetMaterials())
	{
		Material& material = *materialPtr;
		ImGui::PushID(index++);
		if (ImGui::TreeNode(material.name.c_str()))
		{
			const size_t users = scene.CountMaterialUsers(&material);
			if (users > 1) ImGui::TextDisabled("Shared by %zu objects - edits affect all of them", users);
			else ImGui::TextDisabled("Used by %zu object%s", users, users == 1 ? "" : "s");
			ImGui::ColorEdit4("Base color", &material.baseColor.x);
			SliderFloatR("Metallic", &material.metallic, kMaterialDefaults.metallic, 0.0f, 1.0f);   // PBR
			SliderFloatR("Roughness", &material.roughness, kMaterialDefaults.roughness, 0.0f, 1.0f);
			ImGui::ColorEdit3("Emissive", &material.emissive.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
			SliderFloatR("Occlusion strength", &material.occlusionStrength, kMaterialDefaults.occlusionStrength, 0.0f, 1.0f);
			ImGui::TextWrapped("Albedo: %s (%s)   Normal: %s", material.albedoTexture.empty() ? "(none)" : material.albedoTexture.c_str(),
				material.albedoSrgb ? "sRGB" : "linear", material.normalTexture.empty() ? "(none)" : material.normalTexture.c_str());
			ImGui::TextWrapped("Metal/Rough: %s   AO: %s   Emissive: %s",
				material.metallicRoughnessTexture.empty() ? "(none)" : material.metallicRoughnessTexture.c_str(),
				material.occlusionTexture.empty() ? "(none)" : material.occlusionTexture.c_str(),
				material.emissiveTexture.empty() ? "(none)" : material.emissiveTexture.c_str());
			int sampler = static_cast<int>(material.sampler);
			if (ImGui::Combo("Sampler", &sampler, kSamplerNames, ARRAYSIZE(kSamplerNames)))
			{
				material.sampler = static_cast<SamplerPreset>(sampler);
			}
			DragFloat2R("UV scale", &material.uvScale.x, kMaterialDefaults.uvScale.x, kMaterialDefaults.uvScale.y, 0.1f, 0.1f, 64.0f);
			SliderFloatR("Normal strength", &material.normalStrength, kMaterialDefaults.normalStrength, 0.0f, 3.0f);
			DrawTextureImportSettings(material, renderer);
			ImGui::Checkbox("Unlit", &material.unlit);
			SameLineIfFits(CheckboxWidth("Wireframe"));
			ImGui::Checkbox("Wireframe", &material.wireframe);
			SameLineIfFits(CheckboxWidth("Double sided"));
			ImGui::Checkbox("Double sided", &material.doubleSided);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void DebugUI::DrawModelPanel(const char* sceneName, const ModelStats& stats, size_t sceneImages, size_t sceneObjects)
{
	ImGui::Separator();
	ImGui::TextWrapped("Scene: %s   (F11 next: Demo -> DamagedHelmet -> Sponza, or --scene=demo|helmet|sponza)", sceneName);
	if (stats.meshes == 0)
	{
		ImGui::Text("model: none   objects %zu", sceneObjects);
		return;
	}
	ImGui::TextWrapped("model: meshes %u  submeshes %u  instances %u  materials %u  images %u (scene %zu)   load %.0f ms",
		stats.meshes, stats.submeshes, stats.instances, stats.materials, stats.images, sceneImages, stats.loadMilliseconds);
	ImGui::TextWrapped("       vertices %u  triangles %u  16-bit index meshes %u/%u  generated tangents %u  skipped primitives %u   objects %zu",
		stats.vertices, stats.triangles, stats.meshesWith16BitIndices, stats.meshes, stats.generatedTangentMeshes, stats.skippedPrimitives, sceneObjects);
}

// ------------------------------------------------------------------ 엔진 · 프로파일러 · 로그 콘솔

void DebugUI::DrawEnginePanel(Engine& engine)
{
	ImGui::Separator();
	const Config& config = engine.GetConfig();
	const std::string configPath = config.GetPath().empty() ? "(none)" : Log::ToUtf8(config.GetPath().c_str());
	const std::string logPath = Log::GetFilePath().empty() ? "(none)" : Log::ToUtf8(Log::GetFilePath().c_str());
	ImGui::TextWrapped("Engine   backend %s (engine.backend = %s)   config %s", engine.GetDevice().GetBackendName(),
		config.GetString("engine.backend", "?").c_str(), configPath.c_str());

	// 백엔드 선택: engine.ini 의 engine.backend 만 바꿔 저장함. 이미 만든 장치는 그대로이고 다음 실행부터 적용됨.
	// 실행 인자 --backend= 는 ini 보다 우선하므로 인자로 띄운 경우에는 저장해도 다음에 인자가 이김.
	{
		static std::string savedBackend;    // 이 실행에서 저장한 값 ("d3d11"/"d3d12"). 비면 아직 저장 안 함 → 현재 설정값을 보임
		static std::string saveMessage;
		static bool saveFailed = false;
		const char* items[] = { "D3D11", "D3D12" };
		const char* values[] = { "d3d11", "d3d12" };
		const std::string shown = savedBackend.empty() ? config.GetString("engine.backend", "d3d11") : savedBackend;
		int index = (shown == "d3d12") ? 1 : 0;
		ImGui::BeginDisabled(config.GetPath().empty());
		if (ImGui::Combo("Backend (next launch)", &index, items, ARRAYSIZE(items)))
		{
			std::string error;
			if (Config::WriteValue(config.GetPath(), "engine", "backend", values[index], &error))
			{
				savedBackend = values[index];
				saveFailed = false;
				saveMessage = std::string("Saved engine.backend = ") + values[index] + " to engine.ini. Restart the editor to apply.";
				Log::Info("설정 저장: engine.backend = %s (%s). 다음 실행부터 적용됨.", values[index], configPath.c_str());
			}
			else
			{
				saveFailed = true;
				saveMessage = "Save failed: " + error;
				Log::Warn("설정 저장 실패: engine.backend (%s): %s", configPath.c_str(), error.c_str());
			}
		}
		ImGui::EndDisabled();
		if (config.GetPath().empty()) ImGui::TextWrapped("No engine.ini was loaded, so the backend cannot be saved.");
		else if (!saveMessage.empty()) ImGui::TextWrapped("%s", saveMessage.c_str());
		else if (shown != "d3d11" && shown != "d3d12") ImGui::TextWrapped("engine.backend '%s' is not d3d11/d3d12.", shown.c_str());
		if (!savedBackend.empty() && !saveFailed && _stricmp(savedBackend.c_str(), engine.GetDevice().GetBackendName()) != 0)
			ImGui::TextDisabled("Running: %s", engine.GetDevice().GetBackendName());
	}
	ImGui::TextWrapped("Log     file %s   console %s   level %s   info %u  warn %u  error %u", logPath.c_str(),
		Log::HasConsole() ? "yes" : "no", Log::LevelName(Log::GetMinLevel()),
		Log::GetCount(Log::Level::Info), Log::GetCount(Log::Level::Warn), Log::GetCount(Log::Level::Error));
	const AssetManager::Stats& assets = engine.GetAssets().GetStats();
	ImGui::TextWrapped("Assets   models %zu (loads %u, hits %u)   files %zu   %.1f MB cached   %.0f ms total load",
		engine.GetAssets().GetModelCount(), assets.modelLoads, assets.modelHits, engine.GetAssets().GetFileCount(),
		assets.cachedBytes / (1024.0 * 1024.0), assets.totalLoadMs);
	const Time& time = engine.GetTime();
	ImGui::TextWrapped("Time     dt %.2f ms (raw %.2f)   fixed step %.2f ms   steps last frame %u   alpha %.2f",
		time.GetDeltaTime() * 1000.0f, time.GetRawDeltaTime() * 1000.0f, time.GetFixedStep() * 1000.0f,
		time.GetFixedStepsLastFrame(), time.GetInterpolationAlpha());
	if (ImGui::TreeNode("Config values"))
	{
		std::vector<std::pair<std::string, std::string>> values(config.GetAll().begin(), config.GetAll().end());
		std::sort(values.begin(), values.end());
		for (const auto& entry : values) ImGui::TextWrapped("%s = %s", entry.first.c_str(), entry.second.c_str());
		ImGui::TreePop();
	}
}

void DebugUI::DrawProfilerPanel()
{
	ImGui::Separator();
	bool enabled = Profiler::IsEnabled();
	if (ImGui::Checkbox("Profiler", &enabled)) Profiler::SetEnabled(enabled);
	SameLineIfFits(TextItemWidth("CPU frame 0.000 ms   GPU frame 0.000 ms (frame 0)"));
	ImGui::TextWrapped("CPU frame %.3f ms   GPU frame %.3f ms (frame %llu)", Profiler::GetCpuFrameMs(), Profiler::GetGpuFrameMs(), Profiler::GetGpuFrameNumber());
	if (ImGui::TreeNodeEx("Timings", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Columns(2, "profiler", false);
		ImGui::Text("CPU (last frame)");
		for (const Profiler::CpuSample& sample : Profiler::GetCpuSamples())
		{
			ImGui::Text("%*s%-18s %7.3f ms", sample.depth * 2, "", sample.name, sample.milliseconds);
		}
		ImGui::NextColumn();
		ImGui::Text("GPU (timestamp queries)");
		for (const Profiler::GpuSample& sample : Profiler::GetGpuSamples())
		{
			ImGui::Text("%*s%-18s %7.3f ms", sample.depth * 2, "", sample.name, sample.milliseconds);
		}
		ImGui::Columns(1);
		ImGui::TreePop();
	}
}

void DebugUI::DrawLogConsole(bool* open)
{
	static bool showLevel[4] = { false, true, true, true };
	static char filter[128] = {};
	static bool autoScroll = true;
	static uint64_t lastVersion = 0;
	static std::vector<Log::Entry> entries;

	ImGui::SetNextWindowSize(ImVec2(640.0f, 300.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(630.0f, 400.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Console (F12)", open))
	{
		ImGui::End();
		return;
	}
	ImGui::Checkbox("debug", &showLevel[0]); ImGui::SameLine();
	ImGui::Checkbox("info", &showLevel[1]); ImGui::SameLine();
	ImGui::Checkbox("warn", &showLevel[2]); ImGui::SameLine();
	ImGui::Checkbox("error", &showLevel[3]); ImGui::SameLine();
	ImGui::Checkbox("auto-scroll", &autoScroll); ImGui::SameLine();
	if (ImGui::Button("Clear")) Log::ClearHistory();
	ImGui::SameLine();
	ImGui::SetNextItemWidth(200.0f);
	ImGui::InputTextWithHint("##filter", "filter", filter, sizeof(filter));
	ImGui::SameLine();
	int level = static_cast<int>(Log::GetMinLevel());
	ImGui::SetNextItemWidth(90.0f);
	const char* levels[] = { "debug", "info", "warn", "error" };
	if (ImGui::Combo("min level", &level, levels, 4)) Log::SetMinLevel(static_cast<Log::Level>(level));

	const uint64_t version = Log::GetHistoryVersion();
	if (version != lastVersion)
	{
		Log::CopyHistory(entries);
		lastVersion = version;
	}
	ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
	for (const Log::Entry& entry : entries)
	{
		if (!showLevel[static_cast<int>(entry.level)]) continue;
		if (filter[0] != '\0' && entry.text.find(filter) == std::string::npos) continue;
		ImVec4 color(0.25f, 0.25f, 0.25f, 1.0f);
		if (entry.level == Log::Level::Warn) color = ImVec4(0.75f, 0.45f, 0.0f, 1.0f);
		else if (entry.level == Log::Level::Error) color = ImVec4(0.8f, 0.1f, 0.1f, 1.0f);
		else if (entry.level == Log::Level::Debug) color = ImVec4(0.5f, 0.5f, 0.6f, 1.0f);
		ImGui::TextColored(color, "[%8.3f] [%s] %s", entry.time, Log::LevelName(entry.level), entry.text.c_str());
	}
	if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f) ImGui::SetScrollHereY(1.0f);
	ImGui::EndChild();
	ImGui::End();
}

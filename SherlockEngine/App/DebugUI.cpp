#include "pch.h"
#include "App/DebugUI.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "Graphics/Model.h"   // ModelStats
#include "Core/Engine.h"
#include "Core/Config.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "RHI/RHI.h"
#include "App/ContentBrowser.h"   // 2026-10-08: 스카이박스 칸의 드롭 페이로드
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <cfloat>
#include <vector>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	const char* kSamplerNames[] = { "LinearWrap", "LinearClamp", "AnisotropicWrap", "PointWrap", "PointNoMip" };
	const char* kLightTypeNames[] = { "Directional", "Point", "Spot" };
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

void DebugUI::DrawCameraPanel(Camera& camera, float& moveSpeed)
{
	ImGui::Separator();
	ImGui::TextWrapped("Camera  (WASD/QE move, RMB drag look, Shift fast)");
	const XMFLOAT3& p = camera.GetPosition();
	ImGui::Text("pos   %.2f  %.2f  %.2f", p.x, p.y, p.z);
	ImGui::TextWrapped("yaw   %.1f deg   pitch %.1f deg   roll %.1f deg",
		XMConvertToDegrees(camera.GetYaw()), XMConvertToDegrees(camera.GetPitch()), XMConvertToDegrees(camera.GetRoll()));   // roll 은 재생 중 부모가 기울어진 카메라에서만 0 이 아님 (11-F)
	ImGui::SliderFloat("Speed", &moveSpeed, 1.0f, 50.0f);
	// 2026-10-15: 에디터 카메라의 투영 선택. 재생 중 카메라 컴포넌트가 카메라를 움직이면 그 컴포넌트의 설정이 덮어씀.
	bool orthographic = !camera.IsUsePerspectiveProjection();
	if (ImGui::Checkbox("Orthographic", &orthographic)) camera.SetUsePerspectiveProjection(!orthographic);
	if (orthographic)
	{
		float height = camera.GetOrthoHeight();
		if (ImGui::SliderFloat("Ortho height", &height, 1.0f, 100.0f)) camera.SetOrthoHeight(height);
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
	ImGui::TextWrapped("draws %u (+%u shadow)   PSO switches %u   set switches %u   mesh switches %u",
		stats.draws, stats.shadowDraws, stats.pipelineSwitches, stats.resourceSetSwitches, stats.meshSwitches);
	ImGui::TextWrapped("passes %u   barriers %u   PSO cache %zu   GPU meshes %zu   GPU materials %zu   textures %zu   reloads %u   frame slot %u",
		stats.renderPasses, stats.barriers, renderer.GetPipelineCount(), renderer.GetGpuMeshCount(), renderer.GetGpuMaterialCount(),
		renderer.GetTextureCount(), stats.shaderReloads, device.GetFrameIndex());
}

void DebugUI::DrawShadowPanel(Renderer& renderer, RHI::Device& device)
{
	ImGui::Separator();
	RenderSettings& settings = renderer.GetSettings();
	ImGui::Checkbox("Shadows (light 0, directional)", &settings.shadows);
	ImGui::SliderFloat("Shadow bias", &settings.shadowBias, 0.0f, 0.01f, "%.4f");
	ImGui::SliderFloat("Shadow strength", &settings.shadowStrength, 0.0f, 1.0f);
	ImGui::SliderFloat("Ortho size", &settings.shadowOrthoSize, 10.0f, 150.0f);
	ImGui::SliderFloat("Light distance", &settings.shadowDistance, 5.0f, 120.0f);
	if (ImGui::TreeNode("Shadow map (2048x2048 D32, shown as R)"))
	{
		// R32_FLOAT SRV 를 ImGui 가 RGBA 로 샘플하면 빨강 채널에 깊이가 담김. 가까울수록 검고 멀수록 붉게 보임.
		const ImTextureID id = static_cast<ImTextureID>(device.GetImGuiTextureId(renderer.GetShadowMap()));
		if (id != 0)
		{
			ImGui::Image(id, ImVec2(256.0f, 256.0f));
		}
		ImGui::TreePop();
	}
}

namespace
{
	// 2026-10-08: 환경 칸 하나 — 이름 입력(Enter 로 적용) + 콘텐츠 브라우저의 텍스처 드롭.
	// 규칙은 재질 텍스처와 같음: Textures\ 기준 상대 경로 또는 "asset:<Assets 기준>".
	// cube 가 참이면 .dds 는 그 파일(큐브 DDS), 그 밖의 이미지는 그 이미지가 든 폴더(면 6장)를 씀 — 폴더 타일은 드래그할 수 없기 때문.
	// cube 가 거짓(BRDF LUT)이면 떨어뜨린 파일 그대로.
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
	// 이름만 바꾸면 됨 — Renderer 가 다음 프레임에 이름으로 큐브·LUT 를 읽어(캐시) 프레임 셋 t9~t12 에 꽂음. Scene 은 GPU 를 모름.
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
		ImGui::SliderFloat("IBL intensity", &env.iblIntensity, 0.0f, 4.0f);
		ImGui::TextDisabled(env.irradiance.empty() && env.specular.empty()
			? (env.skybox.empty() ? "no environment: ambient color is used" : "no IBL maps: blurred skybox mips are used (approximation)")
			: "IBL maps in use");
		ImGui::TreePop();
	}
	ImGui::SliderFloat("Exposure", &env.exposure, 0.05f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
	SameLineIfFits(CheckboxWidth("ACES tone mapping"));
	ImGui::Checkbox("ACES tone mapping", &env.toneMapping);

	// 2026/10/23: 후처리. 순서는 Renderer 가 고정함 (HDR 구간: 블러 → 블룸, 톤 패스: 노출 → ACES → 색 효과).
	// 환경의 일부라 씬 JSON 과 되돌리기 이력에 함께 들어감.
	if (ImGui::TreeNodeEx("Post process", ImGuiTreeNodeFlags_DefaultOpen))
	{
		PostProcessSettings& post = env.post;
		ImGui::Checkbox("Gaussian blur", &post.blurEnabled);
		if (post.blurEnabled) ImGui::SliderFloat("Blur radius (px @720p)", &post.blurRadius, 0.5f, 32.0f, "%.1f");
		ImGui::Checkbox("Bloom", &post.bloomEnabled);
		if (post.bloomEnabled)
		{
			ImGui::SliderFloat("Bloom threshold", &post.bloomThreshold, 0.0f, 8.0f, "%.2f");
			ImGui::SliderFloat("Bloom knee", &post.bloomKnee, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat("Bloom intensity", &post.bloomIntensity, 0.0f, 4.0f, "%.2f");
		}
		ImGui::SliderFloat("Greyscale", &post.greyscale, 0.0f, 1.0f);
		ImGui::Checkbox("Stage thumbnails", &g_showPostStages);   // 씬이 아니라 에디터 설정 — 되돌리기 이력에 들어가지 않음
		ImGui::TreePop();
	}
}

void DebugUI::DrawLightPanel(Scene& scene)
{
	ImGui::Separator();
	std::vector<LightData>& lights = scene.GetLights();
	ImGui::TextWrapped("Lights (%zu / %u)   light 0 casts the shadow when directional", lights.size(), MAX_LIGHTS);
	ImGui::ColorEdit3("Ambient", &scene.ambientColor.x);
	if (lights.size() < MAX_LIGHTS)
	{
		SameLineIfFits(ButtonWidth("Add point light"));
		if (ImGui::Button("Add point light"))
		{
			LightData light;
			light.type = static_cast<uint32_t>(LightType::Point);
			light.position = XMFLOAT3(0.0f, 6.0f, 0.0f);
			light.range = 20.0f;
			light.intensity = 2.0f;
			lights.push_back(light);
		}
	}

	int removeIndex = -1;
	for (int i = 0; i < static_cast<int>(lights.size()); ++i)
	{
		LightData& light = lights[i];
		ImGui::PushID(i);
		const int typeIndex = static_cast<int>(light.type) < 3 ? static_cast<int>(light.type) : 0;
		if (ImGui::TreeNode("light", "Light %d  %s", i, kLightTypeNames[typeIndex]))
		{
			int selectedType = typeIndex;
			if (ImGui::Combo("Type", &selectedType, kLightTypeNames, ARRAYSIZE(kLightTypeNames)))
			{
				light.type = static_cast<uint32_t>(selectedType);
			}
			if (light.type != static_cast<uint32_t>(LightType::Directional))
			{
				ImGui::DragFloat3("Position", &light.position.x, 0.1f, -60.0f, 60.0f);
				ImGui::DragFloat("Range", &light.range, 0.1f, 0.1f, 200.0f);
			}
			if (light.type != static_cast<uint32_t>(LightType::Point))
			{
				ImGui::DragFloat3("Direction", &light.direction.x, 0.01f, -1.0f, 1.0f);
				SameLineIfFits(ButtonWidth("normalize"));
				if (ImGui::SmallButton("normalize"))
				{
					XMVECTOR d = XMLoadFloat3(&light.direction);
					if (XMVectorGetX(XMVector3LengthSq(d)) > 0.000001f) XMStoreFloat3(&light.direction, XMVector3Normalize(d));
				}
			}
			ImGui::ColorEdit3("Color", &light.color.x);
			ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 10.0f);
			if (light.type == static_cast<uint32_t>(LightType::Spot))
			{
				float inner = XMConvertToDegrees(acosf(light.innerConeCos));
				float outer = XMConvertToDegrees(acosf(light.outerConeCos));
				if (ImGui::SliderFloat("Inner cone (deg)", &inner, 1.0f, 89.0f)) light.innerConeCos = cosf(XMConvertToRadians(inner));
				if (ImGui::SliderFloat("Outer cone (deg)", &outer, 1.0f, 89.0f)) light.outerConeCos = cosf(XMConvertToRadians(outer));
				if (light.outerConeCos > light.innerConeCos) light.outerConeCos = light.innerConeCos;
			}
			if (light.type != static_cast<uint32_t>(LightType::Directional))
			{
				ImGui::DragFloat3("Attenuation (c, l, q)", &light.attenuation.x, 0.001f, 0.0f, 10.0f, "%.4f");
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
	}
}

void DebugUI::DrawMaterialPanel(Scene& scene)
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
			ImGui::ColorEdit4("Base color", &material.baseColor.x);
			ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f);   // 2026-10-08 PBR
			ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f);
			ImGui::ColorEdit3("Emissive", &material.emissive.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
			ImGui::SliderFloat("Occlusion strength", &material.occlusionStrength, 0.0f, 1.0f);
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
			ImGui::DragFloat2("UV scale", &material.uvScale.x, 0.1f, 0.1f, 64.0f);
			ImGui::SliderFloat("Normal strength", &material.normalStrength, 0.0f, 3.0f);
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

// ------------------------------------------------------------------ 10단계

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

#include "pch.h"
#include "App/DebugUI.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "Graphics/Model.h"   // ModelStats
#include "Core/Engine.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "RHI/RHI.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <vector>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	const char* kSamplerNames[] = { "LinearWrap", "LinearClamp", "AnisotropicWrap", "PointWrap", "PointNoMip" };
	const char* kLightTypeNames[] = { "Directional", "Point", "Spot" };
}

void DebugUI::DrawCameraPanel(const Camera& camera, float& moveSpeed)
{
	ImGui::Separator();
	ImGui::Text("Camera  (WASD/QE move, RMB drag look, Shift fast)");
	const XMFLOAT3& p = camera.GetPosition();
	ImGui::Text("pos   %.2f  %.2f  %.2f", p.x, p.y, p.z);
	ImGui::Text("yaw   %.1f deg   pitch %.1f deg",
		XMConvertToDegrees(camera.GetYaw()), XMConvertToDegrees(camera.GetPitch()));
	ImGui::SliderFloat("Speed", &moveSpeed, 1.0f, 50.0f);
	ImGui::Text("F1 wire  F2 cull  F3 vsync  F4 reload  F5 sRGB  F6 sampler  F7 light orbit  F8 shadows  F9 normal maps  F10 freeze");
}

void DebugUI::DrawRenderSettingsPanel(Renderer& renderer, RHI::Device& device)
{
	ImGui::Separator();
	ImGui::Text("Pipeline   backend: %s (--backend=d3d11|d3d12)", device.GetBackendName());
	RenderSettings& settings = renderer.GetSettings();
	ImGui::Checkbox("Wireframe", &settings.wireframe);
	ImGui::SameLine();
	ImGui::Checkbox("Cull back faces", &settings.cullBack);
	ImGui::SameLine();
	ImGui::Checkbox("sRGB output", &settings.srgbOutput);
	ImGui::SameLine();
	ImGui::Checkbox("Normal maps", &settings.normalMapping);
	bool vsync = device.IsVSync();
	if (ImGui::Checkbox("VSync", &vsync))
	{
		device.SetVSync(vsync);
	}
	ImGui::SameLine();
	ImGui::Text(device.IsTearingSupported() ? "(tearing OK)" : "(no tearing)");
	ImGui::SameLine();
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
	ImGui::Text("draws %u (+%u shadow)   PSO switches %u   set switches %u   mesh switches %u",
		stats.draws, stats.shadowDraws, stats.pipelineSwitches, stats.resourceSetSwitches, stats.meshSwitches);
	ImGui::Text("passes %u   barriers %u   PSO cache %zu   GPU meshes %zu   GPU materials %zu   textures %zu   reloads %u   frame slot %u",
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

void DebugUI::DrawLightPanel(Scene& scene)
{
	ImGui::Separator();
	std::vector<LightData>& lights = scene.GetLights();
	ImGui::Text("Lights (%zu / %u)   light 0 casts the shadow when directional", lights.size(), MAX_LIGHTS);
	ImGui::ColorEdit3("Ambient", &scene.ambientColor.x);
	if (lights.size() < MAX_LIGHTS)
	{
		ImGui::SameLine();
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
				ImGui::SameLine();
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
			ImGui::ColorEdit3("Specular", &material.specularColor.x);
			ImGui::SliderFloat("Shininess", &material.shininess, 1.0f, 256.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
			ImGui::Text("Albedo: %s (%s)   Normal: %s", material.albedoTexture.empty() ? "(none)" : material.albedoTexture.c_str(),
				material.albedoSrgb ? "sRGB" : "linear", material.normalTexture.empty() ? "(none)" : material.normalTexture.c_str());
			int sampler = static_cast<int>(material.sampler);
			if (ImGui::Combo("Sampler", &sampler, kSamplerNames, ARRAYSIZE(kSamplerNames)))
			{
				material.sampler = static_cast<SamplerPreset>(sampler);
			}
			ImGui::DragFloat2("UV scale", &material.uvScale.x, 0.1f, 0.1f, 64.0f);
			ImGui::SliderFloat("Normal strength", &material.normalStrength, 0.0f, 3.0f);
			ImGui::Checkbox("Unlit", &material.unlit);
			ImGui::SameLine();
			ImGui::Checkbox("Wireframe", &material.wireframe);
			ImGui::SameLine();
			ImGui::Checkbox("Double sided", &material.doubleSided);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void DebugUI::DrawModelPanel(const char* sceneName, const ModelStats& stats, size_t sceneImages, size_t sceneObjects)
{
	ImGui::Separator();
	ImGui::Text("Scene: %s   (F11 next: Demo -> DamagedHelmet -> Sponza, or --scene=demo|helmet|sponza)", sceneName);
	if (stats.meshes == 0)
	{
		ImGui::Text("model: none   objects %zu", sceneObjects);
		return;
	}
	ImGui::Text("model: meshes %u  submeshes %u  instances %u  materials %u  images %u (scene %zu)   load %.0f ms",
		stats.meshes, stats.submeshes, stats.instances, stats.materials, stats.images, sceneImages, stats.loadMilliseconds);
	ImGui::Text("       vertices %u  triangles %u  16-bit index meshes %u/%u  generated tangents %u  skipped primitives %u   objects %zu",
		stats.vertices, stats.triangles, stats.meshesWith16BitIndices, stats.meshes, stats.generatedTangentMeshes, stats.skippedPrimitives, sceneObjects);
}

// ------------------------------------------------------------------ 10단계

void DebugUI::DrawEnginePanel(Engine& engine)
{
	ImGui::Separator();
	const Config& config = engine.GetConfig();
	const std::string configPath = config.GetPath().empty() ? "(none)" : Log::ToUtf8(config.GetPath().c_str());
	const std::string logPath = Log::GetFilePath().empty() ? "(none)" : Log::ToUtf8(Log::GetFilePath().c_str());
	ImGui::Text("Engine   backend %s (engine.backend = %s)   config %s", engine.GetDevice().GetBackendName(),
		config.GetString("engine.backend", "?").c_str(), configPath.c_str());
	ImGui::Text("Log      file %s   console %s   level %s   info %u  warn %u  error %u", logPath.c_str(),
		Log::HasConsole() ? "yes" : "no", Log::LevelName(Log::GetMinLevel()),
		Log::GetCount(Log::Level::Info), Log::GetCount(Log::Level::Warn), Log::GetCount(Log::Level::Error));
	const AssetManager::Stats& assets = engine.GetAssets().GetStats();
	ImGui::Text("Assets   models %zu (loads %u, hits %u)   files %zu   %.1f MB cached   %.0f ms total load",
		engine.GetAssets().GetModelCount(), assets.modelLoads, assets.modelHits, engine.GetAssets().GetFileCount(),
		assets.cachedBytes / (1024.0 * 1024.0), assets.totalLoadMs);
	const Time& time = engine.GetTime();
	ImGui::Text("Time     dt %.2f ms (raw %.2f)   fixed step %.2f ms   steps last frame %u   alpha %.2f",
		time.GetDeltaTime() * 1000.0f, time.GetRawDeltaTime() * 1000.0f, time.GetFixedStep() * 1000.0f,
		time.GetFixedStepsLastFrame(), time.GetInterpolationAlpha());
	if (ImGui::TreeNode("Config values"))
	{
		std::vector<std::pair<std::string, std::string>> values(config.GetAll().begin(), config.GetAll().end());
		std::sort(values.begin(), values.end());
		for (const auto& entry : values) ImGui::Text("%s = %s", entry.first.c_str(), entry.second.c_str());
		ImGui::TreePop();
	}
}

void DebugUI::DrawProfilerPanel()
{
	ImGui::Separator();
	bool enabled = Profiler::IsEnabled();
	if (ImGui::Checkbox("Profiler", &enabled)) Profiler::SetEnabled(enabled);
	ImGui::SameLine();
	ImGui::Text("CPU frame %.3f ms   GPU frame %.3f ms (frame %llu)", Profiler::GetCpuFrameMs(), Profiler::GetGpuFrameMs(), Profiler::GetGpuFrameNumber());
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

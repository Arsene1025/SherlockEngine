#include "pch.h"
#include "App/DebugUI.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Graphics/Renderer.h"
#include "Graphics/D3D11/Device.h"
#include <imgui.h>

using namespace DirectX;   // 이 파일 안에서만

void DebugUI::DrawCameraPanel(const Camera& camera, float& moveSpeed)
{
	ImGui::Separator();
	ImGui::Text("Camera  (WASD/QE move, RMB drag look, Shift fast)");
	const XMFLOAT3& p = camera.GetPosition();
	ImGui::Text("pos   %.2f  %.2f  %.2f", p.x, p.y, p.z);
	ImGui::Text("yaw   %.1f deg   pitch %.1f deg",
		XMConvertToDegrees(camera.GetYaw()), XMConvertToDegrees(camera.GetPitch()));
	ImGui::SliderFloat("Speed", &moveSpeed, 1.0f, 50.0f);
	ImGui::Text("F1 wireframe  F2 cull  F3 vsync");
}

void DebugUI::DrawRenderSettingsPanel(Renderer& renderer, Device& device)
{
	ImGui::Separator();
	ImGui::Text("Pipeline");
	RenderSettings& settings = renderer.GetSettings();
	ImGui::Checkbox("Wireframe", &settings.wireframe);
	ImGui::Checkbox("Cull back faces", &settings.cullBack);
	bool vsync = device.IsVSync();
	if (ImGui::Checkbox("VSync", &vsync))
	{
		device.SetVSync(vsync);
	}
	ImGui::Text("PSO cache: %zu   GPU meshes: %zu   frame slot: %u",
		renderer.GetPipelineCount(), renderer.GetGpuMeshCount(), device.GetFrameIndex());
}

void DebugUI::DrawLightPanel(Scene& scene)
{
	ImGui::Separator();
	ImGui::Text("Light");
	std::vector<LightData>& lights = scene.GetLights();
	if (lights.empty())
	{
		ImGui::Text("(none)");
		return;
	}

	static const char* lightTypeNames[] = { "Directional", "Point", "Spot" };
	LightData& light = lights[0];
	int selectedType = static_cast<int>(light.type);
	if (ImGui::Combo("Type", &selectedType, lightTypeNames, ARRAYSIZE(lightTypeNames)))
	{
		light.type = static_cast<uint32_t>(selectedType);
	}
	ImGui::SliderFloat3("Position", &light.position.x, -30.0f, 30.0f);
	ImGui::DragFloat3("Direction", &light.direction.x, 0.01f, -1.0f, 1.0f);
	ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 5.0f);
	ImGui::ColorEdit3("Ambient", &scene.ambientColor.x);
}

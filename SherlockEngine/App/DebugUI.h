#pragma once

class Scene;
class Renderer;
class Device;
class Camera;

// ImGui 디버그 패널. 예전에는 Renderer::UpdateGUI가 조명 패널을 갖고 있었다(D3).
// 패널은 데이터를 "편집"하는 쪽이지 데이터를 "소유"하는 쪽이 아니므로 App 계층에 둔다.
namespace DebugUI
{
	void DrawCameraPanel(const Camera& camera, float& moveSpeed);
	void DrawRenderSettingsPanel(Renderer& renderer, Device& device);
	void DrawLightPanel(Scene& scene);
}

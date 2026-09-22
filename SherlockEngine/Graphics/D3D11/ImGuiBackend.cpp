#include "pch.h"
#include "Graphics/D3D11/ImGuiBackend.h"
#include "Graphics/D3D11/Device.h"
#include "Core/Log.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>

bool ImGuiBackend::Init(Device& device)
{
	if (!ImGui_ImplDX11_Init(device.GetDevice(), device.GetContext()))
	{
		Log::Error("ImGui DX11 백엔드 초기화 실패");
		return false;
	}
	return true;
}

void ImGuiBackend::NewFrame()
{
	ImGui_ImplDX11_NewFrame();
}

void ImGuiBackend::Render()
{
	// 이 백엔드는 자기가 건드린 D3D11 상태를 백업하고 복원한다.
	// 그래도 다음 프레임의 첫 드로우 전에 PSO를 다시 바인딩하는 규칙은 유지한다.
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiBackend::Shutdown()
{
	ImGui_ImplDX11_Shutdown();
}

#pragma once

class Device;

// Dear ImGui의 D3D11 렌더러 백엔드 래퍼.
//
// imgui_impl_dx11.h 의 Init은 ID3D11Device*/Context*를 받으므로 그것을 부르는 코드는
// 이 폴더 안에 있어야 한다. AppBase는 Win32 백엔드와 ImGui 컨텍스트만 다루고,
// 그리기는 이 네 함수로만 한다. D3D12 백엔드가 오면 여기만 바뀐다.
namespace ImGuiBackend
{
	bool Init(Device& device);
	void NewFrame();
	void Render();     // ImGui::Render() 이후, Present 이전에 호출
	void Shutdown();
}

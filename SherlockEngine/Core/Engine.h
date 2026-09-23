#pragma once
#include <memory>
#include "RHI/RHI.h"
#include "Graphics/Renderer.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Core/Config.h"
#include "Core/Input.h"
#include "Core/Time.h"
#include "Core/AssetManager.h"

// Engine (10단계, D14): "TestApp 이 엔진을 조립하는 구조"에서 "엔진 위에 앱을 올리는 구조"로.
//
// 소유: RHI Device, Renderer, Scene, Camera, Input, Time, AssetManager, Config. AppBase 는 창과 메시지 루프,
// ImGui 컨텍스트·Win32 백엔드만 갖고 Engine& 을 protected 로 앱에 준다. 앱(TestApp)은 OnUpdate 에서 씬을
// 조작하고 OnGUI 에서 위젯을 더할 뿐, 렌더 순서(BeginFrame → 그림자 → 메인 → UI → Present)는 Engine 이 정한다.
//
// 백엔드는 설정 파일(engine.backend)에서 읽는다. 8단계의 --backend= 는 AppBase 가 설정 값을 덮어쓰는 형태로 남았다.
// God object 를 피하는 규율: 여기에는 소유와 프레임 순서만 있고, 로직은 각 시스템에 남는다.
class Engine
{
public:
	struct Desc
	{
		void* windowHandle = nullptr;
		int width = 0;
		int height = 0;
	};

	Engine();
	~Engine();

	bool Initialize(const Config& config, const Desc& desc);
	void Shutdown();
	bool IsInitialized() const { return m_device != nullptr; }

	// ---- 프레임. AppBase::Run 이 이 순서로 부른다 ----
	float BeginFrame();       // Time.Tick → Profiler 프레임 → ImGui 렌더러 NewFrame. dt 를 돌려준다
	void Render();            // Device BeginFrame → Renderer(그림자·메인) → UI 패스(ImGui::Render 결과) → EndFrame(Present)
	void EndFrame();          // Input 프레임 종료
	void OnResize(int width, int height);
	// 11단계: 다음 프레임의 백버퍼(UI 포함)를 PNG 로. 창이 가려져 있어도 정확하다 (Device 리드백).
	void RequestScreenshot(const std::wstring& path) { m_screenshotPath = path; }

	// ---- 시스템 접근 ----
	RHI::Device& GetDevice() { return *m_device; }
	Renderer& GetRenderer() { return m_renderer; }
	Scene& GetScene() { return m_scene; }
	Camera& GetCamera() { return m_camera; }
	Input& GetInput() { return m_input; }
	Time& GetTime() { return m_time; }
	AssetManager& GetAssets() { return m_assets; }
	const Config& GetConfig() const { return m_config; }
	RHI::Backend GetBackend() const { return m_backend; }

private:
	Config m_config;
	RHI::Backend m_backend = RHI::Backend::D3D11;

	// 선언 순서 = 생성 순서, 파괴는 역순. Renderer 가 Device 보다 뒤에 있어야 먼저 파괴되며 핸들을 돌려준다.
	std::unique_ptr<RHI::Device> m_device;
	Renderer m_renderer;
	AssetManager m_assets;
	Scene m_scene;
	Camera m_camera;
	Input m_input;
	Time m_time;
	bool m_imguiRendererReady = false;
	std::wstring m_screenshotPath;
};

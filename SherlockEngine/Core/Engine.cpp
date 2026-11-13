#include "pch.h"
#include "Core/Engine.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "Graphics/Screenshot.h"
#include "Scene/SkeletonDebug.h"
#include "Scene/SkinPose.h"
#include <imgui.h>

using namespace DirectX;   // 이 파일 안에서만

Engine::Engine()
{
}

Engine::~Engine()
{
	Shutdown();
}

bool Engine::Initialize(const Config& config, const Desc& desc)
{
	m_config = config;
	m_scene.SetAssets(&m_assets);   // Scene::Instantiate 가 프리팹의 모델 메시를 읽을 때 씀

	// ---- 백엔드: 설정 파일이 정함. 값이 올바르지 않으면 D3D11 ----
	const std::string backend = m_config.GetString("engine.backend", "d3d11");
	if (_stricmp(backend.c_str(), "d3d12") == 0) m_backend = RHI::Backend::D3D12;
	else if (_stricmp(backend.c_str(), "d3d11") == 0) m_backend = RHI::Backend::D3D11;
	else { Log::Warn("engine.backend 값 '%s' 을 모름. D3D11 로 실행.", backend.c_str()); m_backend = RHI::Backend::D3D11; }

	RHI::DeviceDesc deviceDesc;
	deviceDesc.windowHandle = desc.windowHandle;
	deviceDesc.width = desc.width;
	deviceDesc.height = desc.height;
	deviceDesc.enableDebugLayer = m_config.GetBool("engine.debuglayer", true);
	m_device = RHI::CreateDevice(m_backend, deviceDesc);
	if (!m_device)
	{
		Log::Error("Engine : Device 초기화 실패 (%s)", RHI::ToString(m_backend));
		return false;
	}
	m_device->SetVSync(m_config.GetBool("engine.vsync", false));

	if (!m_renderer.Initialize(m_device.get()))
	{
		Log::Error("Engine : Renderer 초기화 실패");
		return false;
	}
	// HDR: 게임 모드의 HDR 타깃은 백버퍼와 같은 크기. 에디터는 씬 뷰 크기를 따로 알려 주므로 이 값을 쓰지 않음.
	m_renderer.SetOutputSize(static_cast<uint32_t>(desc.width), static_cast<uint32_t>(desc.height));

	// ImGui 렌더러 백엔드(DX11/DX12)는 Device 가 고름. 컨텍스트는 앱이 먼저 만들어 두어야 함.
	if (ImGui::GetCurrentContext() != nullptr)
	{
		if (!m_device->InitImGui())
		{
			Log::Error("Engine : ImGui 렌더러 백엔드 초기화 실패");
			return false;
		}
		m_imguiRendererReady = true;
	}

	m_camera.SetLens(XM_PIDIV4, desc.height > 0 ? static_cast<float>(desc.width) / desc.height : 16.0f / 9.0f, 0.1f, 1000.0f);
	m_time.SetFixedStep(m_config.GetFloat("engine.fixedstep", 1.0f / 60.0f));
	m_time.Reset();

	Log::Info("Engine : 백엔드 %s, %dx%d, vsync %s, 고정 스텝 %.4f s, 설정 %s",
		m_device->GetBackendName(), desc.width, desc.height, m_device->IsVSync() ? "on" : "off", m_time.GetFixedStep(),
		m_config.GetPath().empty() ? "(기본값)" : Log::ToUtf8(m_config.GetPath().c_str()).c_str());
	return true;
}

void Engine::Shutdown()
{
	if (!m_device) return;
	// 순서: 씬의 GPU 캐시 → Renderer → ImGui 렌더러 → Device. Renderer::Shutdown 이 자기 핸들을 Device 에 돌려줌.
	m_renderer.InvalidateScene(m_scene, true);
	m_scene.Clear();
	m_renderer.Shutdown();
	if (m_imguiRendererReady)
	{
		m_device->ShutdownImGui();
		m_imguiRendererReady = false;
	}
	m_assets.Clear();
	m_device.reset();
}

float Engine::BeginFrame()
{
	const float dt = m_time.Tick();
	Profiler::BeginFrame(m_device.get());
	if (m_imguiRendererReady) m_device->NewFrameImGui();
	return dt;
}

void Engine::Render()
{
	// 프레임 구조: BeginFrame → [ShadowPass → MainPass(HDR) → TonemapPass → UIPass] → EndFrame(Present).
	// Hierarchy 에서 카메라 오브젝트를 고르면 MainPass 뒤에 PreviewPass 가 하나 더 붙음 (Renderer::Render).
	ProfileScope cpuScope("Engine::Render");
	m_device->BeginFrame();
	RHI::CommandList& cmd = m_device->GetCommandList();
	{
		GpuProfileScope gpuFrame(cmd, "Frame");
		{
			ProfileScope cpu("Renderer::Render");
			// 디버그 선: 업데이트 중에 쌓인 선(Raycast 등)에 더해 본 표시를 지금 쌓고, Render 가 올려 그린 뒤 조건 없이 비움.
			// (Render 가 일찍 끝나는 프레임에도 선이 다음 프레임으로 새지 않음)
			ApplyRestSkinPoses(m_scene);   // 팔레트가 비어 있는 스킨 오브젝트(편집 모드 등)에 기본 자세를 채움. 본 표시도 이 포즈를 읽음
			DrawSkeletonDebug(m_debugDraw, m_scene, m_renderer.GetSettings().debugBones);
			m_renderer.Render(m_scene, m_camera, m_time.GetTotalTime(), m_debugDraw);
			m_debugDraw.Clear();
		}
		{
			ProfileScope cpu("UIPass");
			GpuProfileScope gpu(cmd, "UIPass");
			m_renderer.BeginUIPass();          // 백버퍼 UNORM 뷰, 깊이 없음
			if (m_imguiRendererReady) m_device->RenderImGui();
			m_renderer.EndUIPass();            // 패스 종료 + 백버퍼 → Present 전이
		}
	}
	if (!m_screenshotPath.empty()) m_device->RequestBackBufferReadback();
	{
		ProfileScope cpu("Present");
		m_device->EndFrame();
	}
	if (!m_screenshotPath.empty())
	{
		std::vector<uint8_t> pixels;
		uint32_t width = 0, height = 0;
		if (m_device->TakeReadbackResult(pixels, width, height)) Screenshot::SavePng(m_screenshotPath, pixels, width, height);
		else Log::Error("스크린샷: 리드백 결과가 없다 (%s)", Log::ToUtf8(m_screenshotPath.c_str()).c_str());
		m_screenshotPath.clear();
	}
}

void Engine::EndFrame()
{
	// 프레임 끝. Pressed/Released 판정용 이전 상태를 넘기고 마우스 델타를 비움.
	m_input.EndFrame();
}

void Engine::OnResize(int width, int height)
{
	if (!m_device || width <= 0 || height <= 0) return;
	m_device->Resize(width, height);
	m_renderer.SetOutputSize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));   // HDR
	m_camera.SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
}

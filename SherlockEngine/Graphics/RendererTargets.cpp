#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "Graphics/ShaderConstants.h"   // ParticleCollisionConstants
#include "Core/Log.h"
#include <string>

// 뷰 타깃 (Renderer 의 멤버 함수를 파일만 나눠 구현함): 메인 씬 뷰와 카메라 미리보기가 그리는 텍스처 묶음
// (최종 색·깊이·HDR·후처리 입력 셋)의 생성·해제, 그리고 백버퍼에 그리는 UI 패스.
//
// 메인과 미리보기는 같은 함수(CreateViewTarget)로 만듦 — 텍스처를 하나 추가하면 (G-buffer, SSAO) 두 뷰가 함께
// 받음. 깊이도 뷰가 가짐: 게임 모드도 장치의 깊이 버퍼가 아니라 여기서 만든 텍스처에 그림.

bool Renderer::CreateViewTarget(ViewTarget& view, uint32_t width, uint32_t height, bool withColor, const char* name)
{
	DestroyViewTarget(view);
	if (m_device == nullptr || width == 0 || height == 0) return false;

	const std::string base = name ? name : "View";
	const std::string colorName = base + "_Color";
	const std::string depthName = base + "_Depth";
	const std::string hdrName = base + "_HDR";
	const std::string setName = "Set_Post_" + base;

	if (withColor)
	{
		const TextureDesc color{
			.width = width,
			.height = height,
			.format = Format::R8G8B8A8_UNORM,   // TYPELESS 리소스: sRGB RTV 로 그리고 UNORM SRV 로 읽음 (ImGui)
			.mipLevels = 1,
			.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource,
			.debugName = colorName.c_str(),
		};
		view.color = m_device->CreateTexture(color);
	}

	// 깊이도 셰이더에서 읽을 수 있게 만듦 (TYPELESS 리소스 + DSV + SRV). 지금 읽는 패스는 없지만, 깊이를
	// 입력으로 쓰는 패스(SSAO, 디퍼드 라이팅, 소프트 파티클)가 텍스처를 다시 만들지 않고 붙을 수 있음.
	const TextureDesc depth{
		.width = width,
		.height = height,
		.format = Format::D24_UNORM_S8_UINT,
		.mipLevels = 1,
		.bindFlags = TextureBind_DepthStencil | TextureBind_ShaderResource,
		.debugName = depthName.c_str(),
	};
	view.depth = m_device->CreateTexture(depth);
	if (view.depth.IsValid())
	{
		// 파티클이 이 뷰의 깊이를 읽는 셋. 깊이를 다시 만들면 (크기 변경) 셋도 같이 다시 만듦.
		if (!m_particleCollisionCB.IsValid())
		{
			m_particleCollisionCB = m_device->CreateBuffer(BufferDesc{ .size = sizeof(ParticleCollisionConstants), .usage = BufferUsage::Dynamic,
				.bindFlags = BufferBind_Constant, .debugName = "CB_ParticleCollision" });
		}
		view.particleDepthSet = m_device->CreateResourceSet(ResourceSetDesc{
			.layout = m_layouts[Layout_ParticleDepth], .bindings = { { .texture = view.depth } }, .debugName = "Set_ParticleDepth" });
		view.particleCollideSet = m_device->CreateResourceSet(ResourceSetDesc{
			.layout = m_layouts[Layout_ParticleCollide], .bindings = { { .buffer = m_particleCollisionCB }, { .texture = view.depth } },
			.debugName = "Set_ParticleCollide" });
	}

	// 씬 패스가 그리는 FP16 텍스처와, 톤 패스·후처리가 그것을 읽는 셋 (Layout_Post: b0, b4, t13, s6).
	const TextureDesc hdr{
		.width = width,
		.height = height,
		.format = Format::R16G16B16A16_FLOAT,   // 선형 half4. 1 을 넘는 값(태양, 강한 조명)을 잘라내지 않고 담음
		.mipLevels = 1,
		// UAV: 컴퓨트 블러의 세로 패스가 결과를 hdr 에 되돌려 씀 (RenderSettings::computeBlur). FP16 의 typed UAV
		// 쓰기는 FL11_0 필수 지원이라 따로 확인하지 않음 (못 만들면 CreateTexture 가 에러로 알림).
		.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource | TextureBind_UnorderedAccess,
		.debugName = hdrName.c_str(),
	};
	view.hdr = m_device->CreateTexture(hdr);
	if (view.hdr.IsValid())
	{
		view.hdrSet = CreatePostSet(view.hdr, setName.c_str());
	}

	if (!view.IsValid() || (withColor && !view.color.IsValid()))
	{
		DestroyViewTarget(view);
		return false;
	}
	view.width = width;
	view.height = height;

	// 디퍼드 G-buffer + SSAO 타깃 (RendererDeferred.cpp, RendererSsao.cpp). 깊이를 셋에 꽂으므로 깊이 다음에 만듦. 실패해도
	// 뷰는 유효함 — 그 뷰는 포워드로 그림 (UseDeferred 가 HasGBuffer 를 봄).
	CreateGBuffer(view, base);
	return true;
}

void Renderer::DestroyViewTarget(ViewTarget& view)
{
	if (m_device != nullptr)
	{
		// D3D12 는 지연 해제라 이미 기록된 명령(이번 프레임의 ImGui 드로우 등)이 읽어도 안전함. D3D11 은
		// 참조 카운트로 처리됨.
		DestroyGBuffer(view);   // G-buffer 셋이 깊이를 가리키므로 깊이보다 먼저 (SSAO 타깃도 함께)
		if (view.hdrSet.IsValid()) m_device->DestroyResourceSet(view.hdrSet);
		if (view.particleDepthSet.IsValid()) m_device->DestroyResourceSet(view.particleDepthSet);
		if (view.particleCollideSet.IsValid()) m_device->DestroyResourceSet(view.particleCollideSet);
		if (view.hdr.IsValid()) m_device->DestroyTexture(view.hdr);
		if (view.depth.IsValid()) m_device->DestroyTexture(view.depth);
		if (view.color.IsValid()) m_device->DestroyTexture(view.color);
	}
	DestroyPostScratch(view.post);   // 스크래치는 hdr 크기를 따름. 효과가 켜져 있으면 다음 프레임에 다시 만듦
	view = ViewTarget{};
}

void Renderer::SetSceneTarget(uint32_t width, uint32_t height)
{
	if (m_device == nullptr) return;
	if (width == m_sceneWidth && height == m_sceneHeight) return;

	m_sceneWidth = width;
	m_sceneHeight = height;
	DestroyViewTarget(m_mainView);
	if (width == 0 || height == 0) return;   // 오프스크린 끔. 게임 모드 타깃은 다음 Render 가 만듦 (EnsureGameTarget)

	if (!CreateViewTarget(m_mainView, width, height, true, "SceneView"))
	{
		Log::Error("씬 뷰 텍스처 생성 실패 (%ux%u).", width, height);
		m_sceneWidth = m_sceneHeight = 0;
		return;
	}
	Log::Info("씬 뷰 텍스처 %ux%u", width, height);
	Log::Info("HDR 텍스처 %ux%u (R16G16B16A16_FLOAT)", width, height);
}

void Renderer::SetPreviewTarget(uint32_t width, uint32_t height)
{
	if (m_device == nullptr) return;
	if (width == m_previewView.width && height == m_previewView.height) return;

	DestroyViewTarget(m_previewView);
	if (width == 0 || height == 0) return;
	if (!CreateViewTarget(m_previewView, width, height, true, "CameraPreview"))
	{
		Log::Error("카메라 미리보기 텍스처 생성 실패 (%ux%u).", width, height);
	}
}

void Renderer::SetOutputSize(uint32_t width, uint32_t height)
{
	m_outputWidth = width;
	m_outputHeight = height;
}

void Renderer::EnsureGameTarget()
{
	if (m_device == nullptr || m_outputWidth == 0 || m_outputHeight == 0) return;
	if (m_mainView.IsValid() && m_mainView.width == m_outputWidth && m_mainView.height == m_outputHeight) return;

	// 색 텍스처는 만들지 않음 — 톤 패스가 백버퍼에 바로 씀. HDR·깊이는 백버퍼와 같은 크기.
	if (!CreateViewTarget(m_mainView, m_outputWidth, m_outputHeight, false, "Game"))
	{
		Log::Error("HDR 텍스처 생성 실패 (%ux%u).", m_outputWidth, m_outputHeight);
		return;
	}
	Log::Info("HDR 텍스처 %ux%u (R16G16B16A16_FLOAT)", m_outputWidth, m_outputHeight);
}

void Renderer::BeginUIPass()
{
	if (m_device == nullptr) return;
	// ImGui 는 sRGB 로 인코딩된 색을 그대로 출력하므로 UNORM 뷰에 그림. 씬 위에 덧그리므로 Load, 깊이
	// 없음(depth 기본값 = 빈 핸들).
	RenderPassDesc pass{
		.colors = { { .texture = m_device->GetBackBuffer(), .load = LoadOp::Load, .srgbView = false } },
		.colorCount = 1,
		.debugName = "UIPass",
	};
	// 게임 모드에서는 톤 패스가 이미 RenderTarget 으로 두었음 (그때는 아무 일도 없음). 에디터, 또는 이번 프레임에
	// 씬을 그리지 못한 경우에는 여기서 Present → RenderTarget 으로 전이함.
	m_device->GetCommandList().Barrier(m_device->GetBackBuffer(), ResourceState::RenderTarget);
	if (IsOffscreen())
	{
		// 씬이 백버퍼에 그려지지 않았으므로 지움 (에디터 배경).
		pass.colors[0].load = LoadOp::Clear;
		pass.colors[0].clearColor[0] = 0.13f; pass.colors[0].clearColor[1] = 0.13f; pass.colors[0].clearColor[2] = 0.15f; pass.colors[0].clearColor[3] = 1.0f;
	}
	m_device->GetCommandList().BeginRenderPass(pass);
}

void Renderer::EndUIPass()
{
	if (m_device == nullptr) return;
	RHI::CommandList& cmd = m_device->GetCommandList();
	cmd.EndRenderPass();
	// RenderTarget → Present. Present 직전의 마지막 전이.
	cmd.Barrier(m_device->GetBackBuffer(), ResourceState::Present);
	// UI 패스와 마지막 Present 전이까지 센 프레임 전체 값. 패널은 UI 패스 안에서 그려 이 값이 한 프레임 늦게 보임.
	m_stats.renderPasses = cmd.GetStats().renderPasses;
	m_stats.barriers = cmd.GetStats().barriers;
}

#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "Graphics/ShaderConstants.h"
#include "Scene/Scene.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include <string>

// 디퍼드 셰이딩 (2단계, Renderer 의 멤버 함수를 파일만 나눠 구현함). 설계와 읽는 순서:
// docs/plan/deferred-clustered-ssao-record.html.
//
// 디퍼드 프레임의 씬 패스 (RenderDeferredScene):
//   1. GBuffer        view.hdr (발광) + GB0~GB3 + 깊이에 deferred 항목을 그림. 조명은 계산하지 않음
//   2. (배리어)        G-buffer → ShaderResource, 깊이 → DepthRead (라이팅·SSAO 가 SRV 로 읽음)
//   3. Ssao           (켜져 있으면) 깊이·노멀 → ssaoRaw → 블러 → ssaoBlur  (RendererSsao.cpp)
//   4. DeferredLight  풀스크린 삼각형 하나. 픽셀마다 G-buffer → SurfaceData → 포워드와 같은 조명 함수 → view.hdr 에 가산
//   5. Forward        깊이 → DepthWrite. G-buffer 에 못 담는 불투명(unlit·투과·코팅 노멀 맵) → 하늘 → 블렌딩 (먼 것부터)
// 뒤의 파티클·디버그 선 패스(RenderScenePass)는 포워드 경로와 같은 상태(hdr RenderTarget, depth DepthWrite)에서 이어받음.
//
// 포워드와 디퍼드가 같은 재질 함수(MaterialSurface.hlsli)와 조명 함수(Shading.hlsli)를 써서, 두 경로의 화면 차이는
// G-buffer 양자화(8비트 albedo·F0·거칠기, FP16 옥타헤드럴 노멀)와 깊이에서 복원한 위치의 오차만 남음. 측정: 재질 디버그 뷰는
// 최대 1, 조명 화면은 대부분 3 이하. 그림자 계수가 픽셀마다 급변하는 곳(노말맵이 옮긴 지역광 그림자 무늬)에서는 복원 위치의
// 미세한 차이로 비교 결과가 뒤집히는 픽셀이 드문드문 생김 (기록 문서의 검증 절).

namespace
{
	// G-buffer 포맷 (Shaders/GBuffer.hlsli 의 배치 표와 같아야 함). [0] 은 view.hdr (발광).
	constexpr Format kGBufferFormats[4] = {
		Format::R8G8B8A8_UNORM_SRGB,   // GB0 albedo + AO
		Format::R16G16B16A16_FLOAT,    // GB1 옥타헤드럴 노멀 둘
		Format::R8G8B8A8_UNORM_SRGB,   // GB2 F0 + 거칠기
		Format::R8G8B8A8_UNORM,        // GB3 금속성·F90·코팅·코팅 거칠기
	};
	constexpr const char* kGBufferNames[4] = { "_GB0_AlbedoAO", "_GB1_Normals", "_GB2_F0Rough", "_GB3_MetalF90Coat" };
}

bool Renderer::CanUseDeferred(const Material& material) const
{
	// G-buffer 에 담지 못하는 것:
	//   블렌딩 — 한 픽셀에 표면이 여럿 겹쳐야 하는데 G-buffer 는 한 픽셀에 표면 하나
	//   unlit  — 조명을 안 받음 (알베도를 그대로 출력). 라이팅 패스를 거치면 안 됨
	//   투과   — 확산을 줄이고 알파를 바꾸는 값을 저장할 칸이 없음. 지금은 IsBlended 가 투과 재질을 이미 블렌딩으로 분류하므로
	//            아래 검사는 방어용 (IsBlended 규칙이 바뀌어도 G-buffer 로 오지 않게)
	//   코팅 노멀 맵 — 세 번째 노멀을 저장할 칸이 없음 (코팅 노멀 = 기하 노멀로 가정하고 GB1 에 노멀 둘만 둠)
	if (IsBlended(material) || material.unlit) return false;
	if (material.transmissionFactor > 0.0f) return false;
	if (material.clearcoatFactor > 0.0f && !material.clearcoatNormalTexture.empty()) return false;
	return true;
}

bool Renderer::UseDeferred(const ViewTarget& view) const
{
	// 디버그 뷰 5 (UV) 는 G-buffer 에 UV 가 없어 그 프레임 전체를 포워드로 그림.
	return m_settings.deferred && view.HasGBuffer() && m_settings.debugView != 5;
}

bool Renderer::CreateGBuffer(ViewTarget& view, const std::string& base)
{
	for (uint32_t i = 0; i < 4; ++i)
	{
		const std::string name = base + kGBufferNames[i];
		const TextureDesc desc{
			.width = view.width,
			.height = view.height,
			.format = kGBufferFormats[i],
			.mipLevels = 1,
			.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource,
			.debugName = name.c_str(),
		};
		view.gbuffer[i] = m_device->CreateTexture(desc);
		if (!view.gbuffer[i].IsValid())
		{
			Log::Error("G-buffer 생성 실패 (%s, %ux%u). 이 뷰는 포워드로 그림.", name.c_str(), view.width, view.height);
			DestroyGBuffer(view);
			return false;
		}
	}

	// SSAO 타깃은 G-buffer 가 있을 때만 의미가 있음 (노멀을 GB1 에서 읽음). 실패해도 디퍼드는 됨 — t29 에 흰색을 꽂음.
	CreateSsaoTargets(view, base);
	const TextureHandle ao = view.ssaoBlur.IsValid() ? view.ssaoBlur : m_textures.GetWhite();

	// 라이팅 PS 의 G-buffer 셋. 순서 = CreateLayouts 의 gbufferLayout: t23 깊이, t25~t28, t29 SSAO.
	const std::string setName = "Set_GBuffer_" + base;
	view.gbufferSet = m_device->CreateResourceSet(ResourceSetDesc{
		.layout = m_layouts[Layout_GBuffer],
		.bindings = { { .texture = view.depth }, { .texture = view.gbuffer[0] }, { .texture = view.gbuffer[1] }, { .texture = view.gbuffer[2] }, { .texture = view.gbuffer[3] }, { .texture = ao } },
		.debugName = setName.c_str(),
	});
	if (!view.gbufferSet.IsValid())
	{
		Log::Error("G-buffer 셋 생성 실패 (%s). 이 뷰는 포워드로 그림.", setName.c_str());
		DestroyGBuffer(view);
		return false;
	}
	return true;
}

void Renderer::DestroyGBuffer(ViewTarget& view)
{
	if (m_device == nullptr) return;
	DestroySsaoTargets(view);
	if (view.gbufferSet.IsValid())
	{
		m_device->DestroyResourceSet(view.gbufferSet);
	}
	view.gbufferSet = ResourceSetHandle{};
	for (TextureHandle& texture : view.gbuffer)
	{
		if (texture.IsValid())
		{
			m_device->DestroyTexture(texture);
		}
		texture = TextureHandle{};
	}
}

void Renderer::RenderDeferredScene(RHI::CommandList& cmd, const Scene& scene, ViewTarget& view, const Camera& camera, const char* debugName, uint32_t viewIndex)
{
	const std::string base = debugName ? debugName : "View";

	// ---- 1. G-buffer 패스: MRT 5장 + 깊이. 전부 지우고 시작 ----
	// 발광 타깃(view.hdr)은 클리어 색으로 지움 — 라이팅이 깊이 1 픽셀을 버리므로 하늘이 없는 씬은 이 색이 배경으로 남음.
	// G-buffer 의 클리어 값은 의미가 없음 (배경 픽셀은 라이팅이 읽지 않음).
	cmd.Barrier(view.hdr, ResourceState::RenderTarget);
	cmd.Barrier(view.velocity, ResourceState::RenderTarget);
	cmd.Barrier(view.depth, ResourceState::DepthWrite);
	for (const TextureHandle& texture : view.gbuffer)
	{
		cmd.Barrier(texture, ResourceState::RenderTarget);
	}
	{
		const std::string name = base + "GBuffer";
		RenderPassDesc pass{
			.colors = {
				{ .texture = view.hdr, .load = LoadOp::Clear, .srgbView = false, .clearColor = { scene.clearColor[0], scene.clearColor[1], scene.clearColor[2], scene.clearColor[3] } },
				// sRGB 포맷(GB0·GB2)은 텍스처 자체가 _SRGB 라 srgbView 를 켜지 않아도 쓸 때 인코딩함. GB3 는 UNORM(TYPELESS 리소스)
				// 이라 srgbView = false 로 UNORM RTV 를 고름 — 켜면 금속성·F90 이 감마 인코딩된 채 저장됨.
				{ .texture = view.gbuffer[0], .load = LoadOp::Clear, .srgbView = false },
				{ .texture = view.gbuffer[1], .load = LoadOp::Clear, .srgbView = false },
				{ .texture = view.gbuffer[2], .load = LoadOp::Clear, .srgbView = false },
				{ .texture = view.gbuffer[3], .load = LoadOp::Clear, .srgbView = false },
				// 속도 (0 = 움직임 없음). 포워드 패스가 Load 로 이어 씀 (포워드 불투명·하늘)
				{ .texture = view.velocity, .load = LoadOp::Clear, .srgbView = false, .clearColor = { 0.0f, 0.0f, 0.0f, 0.0f } },
			},
			.colorCount = 6,
			.depth = { .texture = view.depth, .load = LoadOp::Clear },
			.debugName = name.c_str(),
		};
		GpuProfileScope gpuScope(cmd, "GBuffer");
		cmd.BeginRenderPass(pass);
		DrawItems(cmd, scene, camera, ObjectCullCamera(camera, viewIndex), DrawMode::GBuffer);
		cmd.EndRenderPass();
	}

	// ---- 2. 읽기 상태로: G-buffer → SRV, 깊이 → DepthRead (DepthRead 는 픽셀 셰이더 SRV 읽기도 포함) ----
	for (const TextureHandle& texture : view.gbuffer)
	{
		cmd.Barrier(texture, ResourceState::ShaderResource);
	}
	cmd.Barrier(view.depth, ResourceState::DepthRead);

	// ---- 3. SSAO (5단계). 꺼져 있어도 G-buffer 셋이 t29 에 ssaoBlur 를 꽂고 있으므로 SRV 상태는 맞춰 둠 ----
	const bool ssao = m_ssaoSettings.enabled && view.HasSsao();
	if (ssao)
	{
		GpuProfileScope gpuScope(cmd, "Ssao");
		RenderSsao(cmd, view);
	}
	else if (view.ssaoBlur.IsValid())
	{
		cmd.Barrier(view.ssaoBlur, ResourceState::ShaderResource);
	}

	// ---- 4. 디퍼드 라이팅: 풀스크린 삼각형, 깊이 없음, view.hdr 에 가산 ----
	{
		const std::string name = base + "DeferredLight";
		const RenderPassDesc pass{
			.colors = { { .texture = view.hdr, .load = LoadOp::Load } },
			.colorCount = 1,
			.debugName = name.c_str(),
		};
		GpuProfileScope gpuScope(cmd, "DeferredLight");
		cmd.BeginRenderPass(pass);
		const PipelineHandle pipeline = GetDeferredLightPipeline();
		if (pipeline.IsValid())
		{
			cmd.SetPipelineState(pipeline);
			cmd.SetResourceSet(kSetFrame, m_frameSet);          // 조명·그림자·IBL·클러스터 (b0 = 이 뷰의 카메라)
			cmd.SetResourceSet(kSetGBuffer, view.gbufferSet);   // t23 깊이, t25~t28, t29 SSAO
			cmd.Draw(3, 0);                                     // 정점 버퍼 없음 — ScreenVS 가 SV_VertexID 로 삼각형을 만듦
			++m_stats.pipelineSwitches;
			++m_stats.draws;
			m_stats.triangles += 1;
		}
		cmd.EndRenderPass();
	}

	// ---- 5. 포워드: 깊이를 다시 쓰기 상태로 (포워드 불투명이 깊이를 씀). 하늘은 G-buffer 깊이로 가려짐 ----
	cmd.Barrier(view.depth, ResourceState::DepthWrite);
	{
		const std::string name = base + "Forward";
		const RenderPassDesc pass{
			.colors = { { .texture = view.hdr, .load = LoadOp::Load }, { .texture = view.velocity, .load = LoadOp::Load } },
			.colorCount = 2,
			.depth = { .texture = view.depth, .load = LoadOp::Load },
			.debugName = name.c_str(),
		};
		cmd.BeginRenderPass(pass);
		DrawItems(cmd, scene, camera, ObjectCullCamera(camera, viewIndex), DrawMode::ForwardRest);   // 셋 1 을 오브젝트 셋으로 되돌려 꽂음 (라이팅이 G-buffer 셋을 꽂았음)
		cmd.EndRenderPass();
	}

	if (viewIndex == 0)
	{
		m_stats.deferredPath = true;
		m_stats.ssao = ssao;
	}
}

PipelineHandle Renderer::GetGBufferPipelineFor(const Material& material, VertexFormat vertexFormat, bool mirrored, bool ocean)
{
	// 메인 PSO (GetPipelineFor) 와 같은 키에서 블렌딩 비트만 뺀 것 — 블렌딩 재질은 G-buffer 로 오지 않음. 비트 12 = 바다 VS.
	const bool wireframe = m_settings.wireframe || material.wireframe;
	const bool cullBack = m_settings.cullBack && !material.doubleSided;
	const bool skinned = vertexFormat == VertexFormat::PositionColorNormalTexcoordTangentSkin;
	const bool oceanVs = ocean && !skinned;
	const uint32_t key = static_cast<uint32_t>(vertexFormat) | (mirrored ? 1u << 8 : 0u) | (wireframe ? 1u << 9 : 0u) | (cullBack ? 1u << 10 : 0u) | (oceanVs ? 1u << 12 : 0u);
	auto found = m_gbufferPipelines.find(key);
	if (found != m_gbufferPipelines.end()) return found->second;

	PipelineStateDesc desc = m_passDesc[skinned ? Pass_GBufferSkin : (oceanVs ? Pass_GBufferOcean : Pass_GBuffer)];
	desc.vertexLayout = GetVertexLayout(vertexFormat);
	desc.rasterizer.frontCounterClockwise = mirrored ? !desc.rasterizer.frontCounterClockwise : desc.rasterizer.frontCounterClockwise;
	desc.rasterizer.fill = wireframe ? FillMode::Wireframe : FillMode::Solid;
	desc.rasterizer.cull = cullBack ? CullMode::Back : CullMode::None;
	// MRT 5장의 포맷을 모두 적음 (D3D12 PSO 는 렌더 타깃 포맷을 알아야 함. 기본값은 [0] 만 있음).
	desc.rtvFormats[0] = Format::R16G16B16A16_FLOAT;
	for (uint32_t i = 0; i < 4; ++i)
	{
		desc.rtvFormats[1 + i] = kGBufferFormats[i];
	}
	desc.rtvFormats[5] = kVelocityFormat;   // 속도 (GBufferPixelShader 의 SV_Target5)
	desc.rtvCount = 6;
	desc.dsvFormat = Format::D24_UNORM_S8_UINT;
	const PipelineHandle handle = m_device->CreatePipeline(desc);
	if (handle.IsValid())
	{
		m_gbufferPipelines.emplace(key, handle);
	}
	return handle;
}

PipelineHandle Renderer::GetDeferredLightPipeline()
{
	if (m_deferredLightPipeline.IsValid()) return m_deferredLightPipeline;
	// FP16 view.hdr 에 가산 (BuildPassDescs 의 Pass_DeferredLight 블렌드). 깊이 없음.
	PipelineStateDesc desc = m_passDesc[Pass_DeferredLight];
	desc.rtvFormats[0] = Format::R16G16B16A16_FLOAT;
	desc.rtvCount = 1;
	m_deferredLightPipeline = m_device->CreatePipeline(desc);
	return m_deferredLightPipeline;
}

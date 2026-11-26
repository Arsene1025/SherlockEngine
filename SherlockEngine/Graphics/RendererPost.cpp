#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "Graphics/ShaderConstants.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

// HDR 구간 후처리 (TAA → 모션 블러 → 가우시안 블러 → 블룸. 디버그 뷰 16·17 이면 속도 표시만 그리고 나머지는 건너뜀). Renderer 의 멤버 함수를 파일만 나눠 구현함 — Renderer.cpp 가 너무 길어지지 않게.
//
// 순서는 고정임 (사용자가 바꿀 수 없음): 선형 HDR 값에서 해야 1 을 넘는 밝기가 살아 있으므로 톤매핑 앞에 둠.
// 효과 하나를 추가할 때: PostProcessSettings 에 필드 → 여기 ApplyHdrEffects 에 호출 하나 → (새 모드가
// 필요하면) PostPixelShader.hlsl 에 모드 하나. 결과는 항상 hdr 텍스처에 "제자리"로 남으므로 톤 패스는 효과가
// 몇 개 켜졌는지 모름.

namespace
{
	constexpr float kReferenceHeight = 720.0f;   // PostProcessSettings 의 블러 반경이 기준으로 삼는 출력 높이 (반경 상한은 ShaderConstants.h 의 kMaxBlurRadius)
}

PipelineHandle Renderer::GetPostPipeline(bool additive)
{
	PipelineHandle& cached = m_postPipelines[additive ? 1 : 0];
	if (cached.IsValid()) return cached;
	// 블러·블룸은 FP16 스크래치/HDR 텍스처에 그림 (깊이 없음 — dsvFormat 은 Pass_Post 의 Unknown 그대로).
	PipelineStateDesc desc = additive ? m_postAddDesc : m_passDesc[Pass_Post];
	desc.rtvFormats[0] = Format::R16G16B16A16_FLOAT;
	desc.rtvCount = 1;
	cached = m_device->CreatePipeline(desc);
	return cached;
}

void Renderer::DispatchBlurCompute(RHI::CommandList& cmd, ComputePipelineHandle pipeline, ResourceSetHandle set, TextureHandle output, uint32_t width, uint32_t height)
{
	// 출력 → UnorderedAccess. 입력(셋의 t13)은 앞 단계가 남긴 ShaderResource 상태 그대로임. D3D11 에서는 이 전이가
	// 출력 텍스처를 SRV 슬롯(PS t13 · CS t13)에서 풀어 줌 — 안 풀면 런타임이 "still bound" 경고와 함께 한쪽(문서상 SRV 쪽)을 NULL 로 만듦.
	cmd.Barrier(output, ResourceState::UnorderedAccess);
	cmd.SetComputePipeline(pipeline);
	cmd.SetComputeResourceSet(kSetBlurCompute, set);      // PostParams 를 올린 뒤에 꽂아야 D3D12 루트 CBV 가 새 주소를 가리킴
	cmd.Dispatch((width + 7) / 8, (height + 7) / 8, 1);   // BlurComputeShader 의 [numthreads(8, 8, 1)]
	// 다음 단계(세로 패스의 입력, 블룸, 톤 패스)가 SRV 로 읽음. D3D11 에서는 CS UAV 슬롯에서 풀어 줌 (UAV 에 남은 채 SRV 로 꽂으면 그 SRV 가 NULL 이 됨).
	cmd.Barrier(output, ResourceState::ShaderResource);
	++m_stats.pipelineSwitches;
}

void Renderer::DispatchBlurWeights(RHI::CommandList& cmd, ComputePipelineHandle pipeline, uint32_t radius)
{
	// 쓰기 전 UnorderedAccess. D3D11 에서는 이 전이가 PS t20 · CS t20 의 SRV 바인딩을 풀어 줌 (같은 버퍼가 SRV 와 UAV 에
	// 동시에 꽂히면 런타임이 경고와 함께 한쪽을 NULL 로 만듦. 문서상 SRV 쪽).
	cmd.Barrier(m_blurWeights, ResourceState::UnorderedAccess);
	cmd.SetComputePipeline(pipeline);
	cmd.SetComputeResourceSet(kSetBlurWeights, m_blurWeightsSet);   // PostParams 를 올린 뒤 (D3D12 루트 CBV 주소)
	cmd.Dispatch((radius + 1 + 63) / 64, 1, 1);                     // BlurWeightsComputeShader 의 [numthreads(64, 1, 1)]. 원소 0..radius
	cmd.Barrier(m_blurWeights, ResourceState::ShaderResource);      // PS·CS 블러가 t20 으로 읽음
}

ResourceSetHandle Renderer::CreatePostSet(TextureHandle input, const char* debugName)
{
	// 슬롯 순서는 CreateLayouts 의 postLayout.Add(...) 순서: b0 PerFrame, b4 PostParams, t13 입력, s6 선형 Clamp, t20 블러 가중치.
	const ResourceSetDesc desc{
		.layout = m_layouts[Layout_Post],
		.bindings = { { .buffer = m_perFrameCB }, { .buffer = m_postParamsCB }, { .texture = input }, { .sampler = GetSampler(SamplerPreset::LinearClamp) }, { .buffer = m_blurWeights } },
		.debugName = debugName,
	};
	return m_device->CreateResourceSet(desc);
}

void Renderer::UploadPostParams(const PostParamsConstants& params)
{
	m_device->UpdateBuffer(m_postParamsCB, &params, sizeof(params));
}

void Renderer::RenderPostPass(RHI::CommandList& cmd, const PostPassDesc& desc)
{
	if (!desc.pipeline.IsValid() || !desc.input.IsValid() || !desc.output.IsValid()) return;

	// 출력: (에디터) 씬 뷰 텍스처 Common/ShaderResource → RenderTarget, (게임) 백버퍼 Present → RenderTarget.
	// D3D12 스왑체인 관례. D3D11 에서는 상태 기록만 함. 출력의 지금 상태는 RHI 가 텍스처마다 기억함.
	cmd.Barrier(desc.output, ResourceState::RenderTarget);

	// 전체 화면을 다 덮어쓰면 Load 도 Clear 도 필요 없음 (DontCare). 덧그리는 패스(블룸 합성)는 Load.
	// sRGB 뷰는 선형 → sRGB 인코딩을 함 (톤 패스만 씀. F5 로 끄면 UNORM 뷰).
	const RenderPassDesc pass{
		.colors = { { .texture = desc.output, .load = desc.load, .srgbView = desc.srgbView } },
		.colorCount = 1,
		.debugName = desc.debugName,
	};
	cmd.BeginRenderPass(pass);
	cmd.SetPipelineState(desc.pipeline);
	cmd.SetResourceSet(kSetPost, desc.input);   // b0 (PerFrame) + b4 (PostParams) + t13 (입력) + s6
	cmd.Draw(3, 0);                             // 정점 버퍼 없음 — ScreenVS 가 SV_VertexID 로 삼각형 하나를 만듦
	++m_stats.pipelineSwitches;
	++m_stats.draws;
	m_stats.triangles += 1;
	cmd.EndRenderPass();

	// ImGui 가 샘플링하거나 (씬 뷰·썸네일), 다음 후처리 패스가 읽거나, UI 패스가 이어서 백버퍼에 그림 (게임).
	cmd.Barrier(desc.output, desc.outputAfter);
}

void Renderer::RenderTonemapPass(RHI::CommandList& cmd, ResourceSetHandle hdrSet, TextureHandle output, ResourceState outputAfter, const char* debugName, uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH, bool skipGrade)
{
	PostParamsConstants params = {};
	params.view = XMFLOAT4(static_cast<float>(srcW), static_cast<float>(srcH), static_cast<float>(dstW), static_cast<float>(dstH));
	params.grade = XMFLOAT4(m_postSettings.greyscale, skipGrade ? 1.0f : 0.0f, 0.0f, 0.0f);
	UploadPostParams(params);   // 효과가 전부 꺼져도 매 프레임 올림 (D3D12 의 갱신 안 된 Dynamic CB 경고 방지)

	RenderPostPass(cmd, PostPassDesc{
							.pipeline = GetTonemapPipeline(),
							.input = hdrSet,
							.output = output,
							.outputAfter = outputAfter,
							.load = LoadOp::DontCare,
							.srgbView = m_settings.srgbOutput,
							.debugName = debugName,
						});
}

PipelineHandle Renderer::GetTemporalPipeline(PassId pass)
{
	PipelineHandle& cached = m_temporalPipelines[pass];
	if (cached.IsValid()) return cached;
	// 시간 셋 풀스크린 패스 (속도 디버그·모션 블러·TAA): FP16 출력 1장 (hdr·스크래치·히스토리), 깊이 없음.
	PipelineStateDesc desc = m_passDesc[pass];
	desc.rtvFormats[0] = Format::R16G16B16A16_FLOAT;
	desc.rtvCount = 1;
	cached = m_device->CreatePipeline(desc);
	return cached;
}

bool Renderer::TaaActive() const
{
	// 디버그 뷰는 조명 결과가 아니라 섞으면 읽을 수 없음 → TAA 도 지터도 끔 (뷰를 끈 첫 프레임은 historyValid = false 로 새로 시작)
	return m_postSettings.taaEnabled && m_settings.debugView == 0;
}

bool Renderer::EnsureTaaTargets(ViewTarget& view)
{
	if (view.history[0].IsValid()) return true;
	if (m_device == nullptr || !view.hdr.IsValid() || !view.velocity.IsValid()) return false;
	for (uint32_t i = 0; i < 2; ++i)
	{
		const std::string name = "TaaHistory" + std::to_string(i);
		view.history[i] = m_device->CreateTexture(TextureDesc{
			.width = view.width,
			.height = view.height,
			.format = Format::R16G16B16A16_FLOAT,   // hdr 와 같은 선형 FP16 (1 을 넘는 밝기를 그대로 섞음)
			.mipLevels = 1,
			.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource,
			.debugName = name.c_str(),
		});
		if (!view.history[i].IsValid()) break;
		view.historyPostSet[i] = CreatePostSet(view.history[i], ("Set_TaaCopy" + std::to_string(i)).c_str());
	}
	for (uint32_t i = 0; i < 2 && view.history[1].IsValid(); ++i)
	{
		view.taaSet[i] = CreateTemporalSet(view, view.hdr, view.history[1 - i], ("Set_Taa" + std::to_string(i)).c_str());   // 읽기 = 다른 쪽
	}
	const bool ok = view.history[0].IsValid() && view.history[1].IsValid() && view.taaSet[0].IsValid() && view.taaSet[1].IsValid() && view.historyPostSet[0].IsValid() && view.historyPostSet[1].IsValid();
	if (!ok)
	{
		Log::Error("TAA 히스토리 생성 실패 (%ux%u). TAA 를 건너뜀.", view.width, view.height);
		DestroyTaaTargets(view);
		return false;
	}
	view.temporal.historyValid = false;
	view.temporal.current = 0;
	Log::Info("TAA 히스토리 %ux%u x 2 (R16G16B16A16_FLOAT)", view.width, view.height);
	return true;
}

void Renderer::DestroyTaaTargets(ViewTarget& view)
{
	if (m_device != nullptr)
	{
		for (uint32_t i = 0; i < 2; ++i)
		{
			if (view.taaSet[i].IsValid())
			{
				m_device->DestroyResourceSet(view.taaSet[i]);
			}
			if (view.historyPostSet[i].IsValid())
			{
				m_device->DestroyResourceSet(view.historyPostSet[i]);
			}
			if (view.history[i].IsValid())
			{
				m_device->DestroyTexture(view.history[i]);
			}
		}
	}
	for (uint32_t i = 0; i < 2; ++i)
	{
		view.taaSet[i] = ResourceSetHandle{};
		view.historyPostSet[i] = ResourceSetHandle{};
		view.history[i] = TextureHandle{};
	}
	view.temporal.historyValid = false;
}

bool Renderer::EnsurePostScratch(PostScratch& scratch, TextureHandle hdr, uint32_t width, uint32_t height, bool needTemp, bool needBloom)
{
	if (m_device == nullptr || width == 0 || height == 0) return false;
	if (scratch.width != width || scratch.height != height)
	{
		DestroyPostScratch(scratch);   // hdr 크기가 바뀌었으면 전부 다시
	}

	// 후처리 텍스처는 전부 FP16 — 1 을 넘는 밝기를 잘라내지 않도록 hdr 와 같은 포맷.
	// extraBind: 블러 스크래치는 컴퓨트 블러의 출력(가로 패스)이기도 하므로 UAV 를 함께 켬.
	auto create = [this](uint32_t w, uint32_t h, const char* name, TextureHandle& texture, ResourceSetHandle& set, uint8_t extraBind = 0)
	{
		const TextureDesc desc{
			.width = w,
			.height = h,
			.format = Format::R16G16B16A16_FLOAT,
			.mipLevels = 1,
			.bindFlags = static_cast<uint8_t>(TextureBind_RenderTarget | TextureBind_ShaderResource | extraBind),
			.debugName = name,
		};
		texture = m_device->CreateTexture(desc);
		if (texture.IsValid())
		{
			set = CreatePostSet(texture, name);
		}
		return texture.IsValid() && set.IsValid();
	};

	// temp: 블러의 가로 패스 결과, 모션 블러의 출력 (둘 다 hdr 크기 FP16)
	if (needTemp && !scratch.temp.IsValid())
	{
		if (!create(width, height, "PostTemp", scratch.temp, scratch.tempSet, TextureBind_UnorderedAccess))
		{
			Log::Error("후처리 스크래치 텍스처 생성 실패 (%ux%u).", width, height);
			DestroyPostScratch(scratch);
			return false;
		}
		Log::Info("후처리 스크래치(블러·모션 블러) %ux%u (R16G16B16A16_FLOAT)", width, height);

		// 컴퓨트 블러 셋. 슬롯 순서는 CreateLayouts 의 blurLayout.Add(...) 순서: b4, t13 입력, u0 출력, t20 가중치.
		// 실패해도 (UAV 를 못 만드는 장치 등) 스크래치는 그대로 쓰고 PS 블러로 감.
		const ResourceSetDesc horizontal{
			.layout = m_layouts[Layout_BlurCompute],
			.bindings = { { .buffer = m_postParamsCB }, { .texture = hdr }, { .texture = scratch.temp }, { .buffer = m_blurWeights } },
			.debugName = "BlurComputeH",
		};
		const ResourceSetDesc vertical{
			.layout = m_layouts[Layout_BlurCompute],
			.bindings = { { .buffer = m_postParamsCB }, { .texture = scratch.temp }, { .texture = hdr }, { .buffer = m_blurWeights } },
			.debugName = "BlurComputeV",
		};
		scratch.blurComputeSets[0] = m_device->CreateResourceSet(horizontal);
		scratch.blurComputeSets[1] = m_device->CreateResourceSet(vertical);
	}
	if (needBloom && scratch.bloomCount == 0)
	{
		// 레벨 i = hdr 의 1/2^(i+1). 변이 4 미만이 되면 거기서 멈춤 (너무 작은 레벨은 의미가 없음).
		uint32_t w = width, h = height;
		for (uint32_t i = 0; i < PostScratch::kBloomLevels; ++i)
		{
			w = (std::max)(1u, (w + 1) / 2);
			h = (std::max)(1u, (h + 1) / 2);
			PostScratch::BloomLevel& level = scratch.bloom[i];
			if (!create(w, h, "PostBloom", level.texture, level.set))
			{
				Log::Error("후처리 스크래치 텍스처 생성 실패 (블룸 레벨 %u, %ux%u).", i, w, h);
				DestroyPostScratch(scratch);
				return false;
			}
			level.width = w;
			level.height = h;
			scratch.bloomCount = i + 1;
			if (w < 4 || h < 4) break;
		}
		Log::Info("후처리 스크래치(블룸) 레벨 %u개, 가장 큰 쪽 %ux%u", scratch.bloomCount, scratch.bloom[0].width, scratch.bloom[0].height);
	}
	scratch.width = width;
	scratch.height = height;
	return true;
}

void Renderer::DestroyPostScratch(PostScratch& scratch)
{
	if (m_device != nullptr)
	{
		// D3D12 는 지연 해제라 이미 기록된 명령이 읽어도 안전함.
		if (scratch.tempSet.IsValid())
		{
			m_device->DestroyResourceSet(scratch.tempSet);
		}
		for (ResourceSetHandle& set : scratch.blurComputeSets)
		{
			if (set.IsValid())
			{
				m_device->DestroyResourceSet(set);
			}
		}
		if (scratch.temp.IsValid())
		{
			m_device->DestroyTexture(scratch.temp);
		}
		for (PostScratch::BloomLevel& level : scratch.bloom)
		{
			if (level.set.IsValid())
			{
				m_device->DestroyResourceSet(level.set);
			}
			if (level.texture.IsValid())
			{
				m_device->DestroyTexture(level.texture);
			}
		}
	}
	scratch = PostScratch{};
}

void Renderer::RequestPostCapture()
{
	m_postCaptureRequested = true;
	// 썸네일 텍스처는 여기(UI 를 만드는 중)에서 만들고 바꿈. Render 에서 바꾸면 이 프레임의 UI 가 이미 집어
	// 든 ImGui 텍스처 ID(D3D11 은 SRV 포인터)가 UI 패스가 그리기 전에 해제됨. 씬 뷰 텍스처(SetSceneTarget)를
	// UI 단계에서 바꾸는 것과 같은 규칙임.
	if (IsOffscreen())
	{
		EnsureStageThumbs(m_sceneWidth, m_sceneHeight);
	}
}

void Renderer::EnsureStageThumbs(uint32_t hdrWidth, uint32_t hdrHeight)
{
	if (m_device == nullptr || hdrWidth == 0 || hdrHeight == 0) return;
	// 폭 384 (HDR 가 더 좁으면 그 폭), 높이는 같은 종횡비. 종횡비가 바뀌면(씬 뷰 리사이즈) 다시 만듦.
	const uint32_t width = (std::min)(384u, hdrWidth);
	const uint32_t height = (std::max)(1u, static_cast<uint32_t>(static_cast<uint64_t>(width) * hdrHeight / hdrWidth));
	if (width == m_thumbWidth && height == m_thumbHeight && m_stageThumbs[0].texture.IsValid()) return;

	DestroyStageThumbs();
	static const char* const kNames[kPostStageCount] = { "Scene", "TAA", "Motion", "Blur", "Bloom", "Final" };   // PostStage 순서
	for (uint32_t i = 0; i < kPostStageCount; ++i)
	{
		const TextureDesc desc{
			.width = width,
			.height = height,
			.format = Format::R8G8B8A8_UNORM,   // 씬 뷰와 같은 TYPELESS 규칙 (sRGB RTV + UNORM SRV 로 ImGui 가 읽음)
			.mipLevels = 1,
			.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource,
			.debugName = "PostStageThumb",
		};
		m_stageThumbs[i].texture = m_device->CreateTexture(desc);
		m_stageThumbs[i].name = kNames[i];
		if (!m_stageThumbs[i].texture.IsValid())
		{
			Log::Error("후처리 단계 썸네일 생성 실패 (%ux%u).", width, height);
			DestroyStageThumbs();
			return;
		}
	}
	m_thumbWidth = width;
	m_thumbHeight = height;
	Log::Info("후처리 단계 썸네일 %ux%u x %u", width, height, kPostStageCount);
}

void Renderer::DestroyStageThumbs()
{
	for (uint32_t i = 0; i < kPostStageCount; ++i)
	{
		if (m_device != nullptr && m_stageThumbs[i].texture.IsValid())
		{
			m_device->DestroyTexture(m_stageThumbs[i].texture);   // D3D12 는 지연 해제 (ImGui 가 읽을 수 있음)
		}
		m_stageThumbs[i] = PostStageThumb{};
	}
	m_thumbWidth = m_thumbHeight = 0;
}

void Renderer::CapturePostStage(RHI::CommandList& cmd, uint32_t stage, const char* name, ResourceSetHandle hdrSet, uint32_t hdrWidth, uint32_t hdrHeight, bool skipGrade)
{
	PostStageThumb& thumb = m_stageThumbs[stage];
	if (!thumb.texture.IsValid() || m_thumbWidth == 0) return;
	// 지금까지의 HDR 결과를 노출·ACES 로 톤매핑해 작은 텍스처에 그림 (톤 패스의 셰이더가 입력/출력 크기 비로
	// 읽음 = 최근접 축소). 단계 0~2 는 색 효과를 건너뜀 — "이 효과가 한 일"만 보이게. 마지막 Final 만 색
	// 효과까지 포함.
	RenderTonemapPass(cmd, hdrSet, thumb.texture, ResourceState::ShaderResource, name, hdrWidth, hdrHeight, m_thumbWidth, m_thumbHeight, skipGrade);
	thumb.active = true;
}

void Renderer::ApplyHdrEffects(RHI::CommandList& cmd, ViewTarget& view, bool capture)
{
	PostScratch& scratch = view.post;
	const TextureHandle hdr = view.hdr;
	const ResourceSetHandle hdrSet = view.hdrSet;
	const uint32_t width = view.width;
	const uint32_t height = view.height;
	const PostProcessSettings& post = m_postSettings;
	bool blur = post.blurEnabled && post.blurRadius > 0.0f;
	bool bloom = post.bloomEnabled && post.bloomIntensity > 0.0f;
	// 모션 블러: 디버그 뷰(알베도·노멀·속도 …)에서는 건너뜀 — 디버그 색이 번지면 읽을 수 없음
	bool motionBlur = post.motionBlurEnabled && post.motionBlurIntensity > 0.0f && m_settings.debugView == 0 && view.temporalSet.IsValid();
	// TAA: 꺼졌거나 디버그 뷰인 프레임은 히스토리를 버림 → 다시 켠 첫 프레임은 이번 프레임을 그대로 히스토리로 씀
	bool taa = TaaActive() && view.temporalSet.IsValid();
	if (taa && !EnsureTaaTargets(view))
	{
		taa = false;
	}
	if (!taa)
	{
		view.temporal.historyValid = false;
	}
	// 블러는 PS·CS 두 경로 모두 컴퓨트로 쓴 가중치 버퍼를 읽음. 가중치 PSO 를 못 만들면 블러를 건너뜀.
	const ComputePipelineHandle blurWeights = blur ? GetComputePipeline(ComputePass_BlurWeights) : ComputePipelineHandle{};
	if (blur && !blurWeights.IsValid())
	{
		static bool warned = false;
		if (!warned)
		{
			Log::Error("블러 가중치 컴퓨트 PSO 가 없어 블러를 건너뜀.");
		}
		warned = true;
		blur = false;
	}
	if ((blur || bloom || motionBlur) && !EnsurePostScratch(scratch, hdr, width, height, blur || motionBlur, bloom))
	{
		blur = bloom = motionBlur = false;   // 못 만들면 효과만 건너뜀 (씬은 그대로 나감)
	}
	if (capture)
	{
		CapturePostStage(cmd, PostStage_Scene, "Scene", hdrSet, width, height, true);
	}

	const float w = static_cast<float>(width);
	const float h = static_cast<float>(height);

	// 후처리가 깊이(t23)·속도(t7)를 읽음. 씬 패스가 깊이를 DepthWrite/DepthRead 로 남기므로 SRV 로 읽을 수 있는 DepthRead 로
	// 둠 (다음 프레임 씬 패스 첫 배리어가 DepthWrite 로 되돌림). 속도는 씬 패스 끝에서 이미 ShaderResource.
	cmd.Barrier(view.depth, ResourceState::DepthRead);

	// ---- 디버그 뷰 16·17: 속도 버퍼를 hdr 에 그대로 그리고 다른 효과는 건너뜀 (디버그 색이 번지면 읽을 수 없음) ----
	if (m_settings.debugView == 16 || m_settings.debugView == 17)
	{
		const PipelineHandle pipeline = GetTemporalPipeline(Pass_VelocityDebug);
		if (pipeline.IsValid() && view.velocityDebugSet.IsValid())
		{
			PostParamsConstants params = {};
			params.view = XMFLOAT4(w, h, w, h);
			UploadPostParams(params);
			RenderPostPass(cmd, PostPassDesc{
									.pipeline = pipeline,
									.input = view.velocityDebugSet,   // t13 = 흰 텍스처 (출력이 hdr 라 hdr 를 입력으로 꽂지 않음)
									.output = hdr,
									.outputAfter = ResourceState::ShaderResource,
									.load = LoadOp::DontCare,
									.debugName = "VelocityDebug",
								});
		}
		if (capture)
		{
			CapturePostStage(cmd, PostStage_Final, "Final", hdrSet, width, height, false);
		}
		return;
	}

	// ---- TAA: hdr (지터된 이번 프레임) + history[1−cur] → history[cur], 그 다음 history[cur] → hdr 복사 (모드 0). 다음 프레임은
	// 방금 쓴 쪽을 읽음. 모션 블러보다 먼저 — 지터가 남은 이미지를 정리한 뒤 번지게 함 ----
	const PipelineHandle taaPipeline = taa ? GetTemporalPipeline(Pass_Taa) : PipelineHandle{};
	if (taaPipeline.IsValid())
	{
		GpuProfileScope gpuScope(cmd, "TaaPass");
		const uint32_t current = view.temporal.current;
		cmd.Barrier(view.history[current ^ 1u], ResourceState::ShaderResource);   // 처음 만든 직후에는 한 번도 쓰지 않은 상태
		PostParamsConstants params = {};
		params.blur = XMFLOAT4(std::clamp(post.taaFeedback, 0.02f, 1.0f), 0.0f, 0.0f, 0.0f);   // 이번 프레임 비중
		params.view = XMFLOAT4(w, h, w, h);
		UploadPostParams(params);
		RenderPostPass(cmd, PostPassDesc{
								.pipeline = taaPipeline,
								.input = view.taaSet[current],   // t13 hdr, t23 깊이, t7 속도, t24 history[1−cur]
								.output = view.history[current],
								.outputAfter = ResourceState::ShaderResource,
								.load = LoadOp::DontCare,
								.debugName = "TaaResolve",
							});
		params.pass = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);   // 모드 0: 복사
		UploadPostParams(params);
		RenderPostPass(cmd, PostPassDesc{
								.pipeline = GetPostPipeline(false),
								.input = view.historyPostSet[current],
								.output = hdr,
								.outputAfter = ResourceState::ShaderResource,
								.load = LoadOp::DontCare,
								.debugName = "TaaCopy",
							});
		view.temporal.historyValid = true;
		view.temporal.current = current ^ 1u;
		if (capture)
		{
			CapturePostStage(cmd, PostStage_Taa, "TAA", hdrSet, width, height, true);
		}
	}
	else
	{
		view.temporal.historyValid = false;
	}

	// ---- 모션 블러: hdr (+ 깊이·속도) → temp, 그 다음 temp → hdr 복사 (PS_Post 모드 0). 길이 상한은 720p 기준이라 높이에 비례 ----
	const PipelineHandle motionBlurPipeline = motionBlur ? GetTemporalPipeline(Pass_MotionBlur) : PipelineHandle{};
	if (motionBlurPipeline.IsValid())
	{
		GpuProfileScope gpuScope(cmd, "MotionBlurPass");
		PostParamsConstants params = {};
		// 기준 fps 보정: 속도 버퍼는 "지난 프레임 → 이번 프레임" 이동량이라 프레임이 빠를수록 짧음. (1/targetFps) / dt 를 셔터에
		// 곱해 기준 fps 의 한 프레임 이동량으로 늘리거나 줄임 (프레임이 느리면 줄어듦). dt 가 0 에 가까우면(일시정지 등) 보정 안 함.
		// 길이는 셰이더에서 maxRadius 로 다시 자름.
		const float frameDt = m_settings.frameDeltaOverride > 0.0f ? m_settings.frameDeltaOverride : m_frameDeltaTime;
		const float fpsScale = (post.motionBlurTargetFps > 0.0f && frameDt > 1e-5f) ? 1.0f / (post.motionBlurTargetFps * frameDt) : 1.0f;
		params.blur = XMFLOAT4((std::min)(post.motionBlurIntensity, 1.0f) * fpsScale, (std::max)(post.motionBlurMaxRadius, 1.0f) * h / kReferenceHeight, static_cast<float>(std::clamp(post.motionBlurSamples, 4, 32)), 0.0f);
		params.view = XMFLOAT4(w, h, w, h);
		UploadPostParams(params);
		RenderPostPass(cmd, PostPassDesc{
								.pipeline = motionBlurPipeline,
								.input = view.temporalSet,   // t13 hdr, t23 깊이, t7 속도
								.output = scratch.temp,
								.outputAfter = ResourceState::ShaderResource,
								.load = LoadOp::DontCare,
								.debugName = "MotionBlur",
							});
		params.pass = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);   // 모드 0: 복사
		UploadPostParams(params);
		RenderPostPass(cmd, PostPassDesc{
								.pipeline = GetPostPipeline(false),
								.input = scratch.tempSet,
								.output = hdr,
								.outputAfter = ResourceState::ShaderResource,
								.load = LoadOp::DontCare,
								.debugName = "MotionBlurCopy",
							});
		if (capture)
		{
			CapturePostStage(cmd, PostStage_Motion, "Motion", hdrSet, width, height, true);
		}
	}

	// ---- 가우시안 블러: 가로 (hdr → temp) 후 세로 (temp → hdr). 반경은 720p 기준이라 출력 높이에 비례해 스케일함 ----
	if (blur)
	{
		GpuProfileScope gpuScope(cmd, "BlurPass");
		const float effectiveRadius = (std::min)(post.blurRadius * h / kReferenceHeight, static_cast<float>(kMaxBlurRadius));
		PostParamsConstants params = {};
		const float radius = std::ceil((std::max)(effectiveRadius, 1.0f));
		params.blur = XMFLOAT4((std::max)(effectiveRadius / 2.5f, 0.5f), radius, 0.0f, 0.0f);   // 시그마, 반경 (±2.5σ 에서 자름)
		params.view = XMFLOAT4(w, h, w, h);

		// 컴퓨트 경로: 설정이 켜져 있고 PSO·셋이 모두 있을 때만. 아니면 PS 경로.
		// 같은 PostParams·같은 가중치 버퍼를 읽고 더하는 순서가 같아 두 경로의 결과가 비트 단위로 같아야 함.
		const ComputePipelineHandle blurCompute = m_settings.computeBlur ? GetComputePipeline(ComputePass_Blur) : ComputePipelineHandle{};
		const bool useCompute = blurCompute.IsValid() && scratch.blurComputeSets[0].IsValid() && scratch.blurComputeSets[1].IsValid();
		if (scratch.blurPath != (useCompute ? 1 : 0))
		{
			scratch.blurPath = useCompute ? 1 : 0;
			Log::Info("블러 경로: %s (%ux%u)", useCompute ? "컴퓨트 (BlurCS)" : "픽셀 셰이더 (PostPS)", width, height);
		}

		params.pass = XMFLOAT4(1.0f, 1.0f, 0.0f, 0.0f);   // 모드 1, 방향 (1, 0)
		UploadPostParams(params);
		// 이 뷰의 시그마로 가중치를 씀 (뷰마다 높이가 달라 시그마가 다름 — 미리보기 뷰가 다시 씀).
		DispatchBlurWeights(cmd, blurWeights, static_cast<uint32_t>(radius));
		if (useCompute)
		{
			DispatchBlurCompute(cmd, blurCompute, scratch.blurComputeSets[0], scratch.temp, width, height);
			params.pass = XMFLOAT4(1.0f, 0.0f, 1.0f, 0.0f);   // 방향 (0, 1). 결과를 hdr 에 되돌려 씀
			UploadPostParams(params);
			DispatchBlurCompute(cmd, blurCompute, scratch.blurComputeSets[1], hdr, width, height);
		}
		else
		{
			RenderPostPass(cmd, PostPassDesc{
									.pipeline = GetPostPipeline(false),
									.input = hdrSet,
									.output = scratch.temp,
									.outputAfter = ResourceState::ShaderResource,
									.load = LoadOp::DontCare,
									.debugName = "BlurH",
								});

			params.pass = XMFLOAT4(1.0f, 0.0f, 1.0f, 0.0f);   // 방향 (0, 1). 결과를 hdr 에 되돌려 씀
			UploadPostParams(params);
			RenderPostPass(cmd, PostPassDesc{
									.pipeline = GetPostPipeline(false),
									.input = scratch.tempSet,
									.output = hdr,
									.outputAfter = ResourceState::ShaderResource,
									.load = LoadOp::DontCare,
									.debugName = "BlurV",
								});
		}
		if (capture)
		{
			CapturePostStage(cmd, PostStage_Blur, "Blur", hdrSet, width, height, true);
		}
	}

	// ---- 블룸: 밝기 추출 + 축소 (hdr → 레벨 0) → 축소 체인 → 확대하며 가산 (레벨 n-1 → … → 0) → hdr 에 intensity 만큼 가산 ----
	// 마지막 가산이 hdr 를 렌더 타깃으로 쓰고(Load) 입력은 레벨 0 이라, 같은 텍스처를 읽고 쓰는 일이 없음.
	if (bloom && scratch.bloomCount > 0)
	{
		GpuProfileScope gpuScope(cmd, "BloomPass");
		PostScratch::BloomLevel* levels = scratch.bloom;
		const uint32_t count = scratch.bloomCount;

		PostParamsConstants params = {};
		params.bloom = XMFLOAT4(post.bloomThreshold, post.bloomThreshold * post.bloomKnee, 1.0f, 0.0f);

		auto run = [&](float mode, ResourceSetHandle input, float srcW, float srcH, PostScratch::BloomLevel* outLevel, const char* name, bool additive, float scale)
		{
			params.pass = XMFLOAT4(mode, 0.0f, 0.0f, 0.0f);
			params.view = XMFLOAT4(srcW, srcH, static_cast<float>(outLevel->width), static_cast<float>(outLevel->height));
			params.bloom.z = scale;
			UploadPostParams(params);
			RenderPostPass(cmd, PostPassDesc{
									.pipeline = GetPostPipeline(additive),
									.input = input,
									.output = outLevel->texture,
									.outputAfter = ResourceState::ShaderResource,
									.load = additive ? LoadOp::Load : LoadOp::DontCare,
									.debugName = name,
								});
		};

		run(2.0f, hdrSet, w, h, &levels[0], "BloomPrefilter", false, 1.0f);
		for (uint32_t i = 1; i < count; ++i)
		{
			run(3.0f, levels[i - 1].set, static_cast<float>(levels[i - 1].width), static_cast<float>(levels[i - 1].height), &levels[i], "BloomDown", false, 1.0f);
		}
		for (uint32_t i = count - 1; i-- > 0;)
		{
			run(4.0f, levels[i + 1].set, static_cast<float>(levels[i + 1].width), static_cast<float>(levels[i + 1].height), &levels[i], "BloomUp", true, 1.0f);
		}

		// 합성: 레벨 0 → hdr (가산, intensity)
		params.pass = XMFLOAT4(4.0f, 0.0f, 0.0f, 0.0f);
		params.view = XMFLOAT4(static_cast<float>(levels[0].width), static_cast<float>(levels[0].height), w, h);
		params.bloom.z = post.bloomIntensity;
		UploadPostParams(params);
		RenderPostPass(cmd, PostPassDesc{
								.pipeline = GetPostPipeline(true),
								.input = levels[0].set,
								.output = hdr,
								.outputAfter = ResourceState::ShaderResource,
								.load = LoadOp::Load,
								.debugName = "BloomComposite",
							});
		if (capture)
		{
			CapturePostStage(cmd, PostStage_Bloom, "Bloom", hdrSet, width, height, true);
		}
	}
	if (capture)
	{
		CapturePostStage(cmd, PostStage_Final, "Final", hdrSet, width, height, false);
	}
}

#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "Graphics/ShaderConstants.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <string>

using namespace DirectX;   // 이 파일 안에서만

// SSAO (5단계, Renderer 의 멤버 함수를 파일만 나눠 구현함). 설계와 읽는 순서: docs/plan/deferred-clustered-ssao-record.html.
//
//   CreateSsaoTargets    뷰마다 R8 타깃 둘 (ssaoRaw = 노이즈 섞인 원본, ssaoBlur = 블러 결과) 과 셋 둘
//   UploadSsaoConstants  b12: 반경·바이어스·세기·거듭제곱·샘플 수 + 반구 커널 (샘플 수가 바뀔 때만 다시 만듦)
//   RenderSsao           깊이(t23)·노멀(t26) → ssaoRaw → 4×4 블러 → ssaoBlur. 디퍼드 라이팅이 t29 로 읽음
// 셰이더: Shaders/Ssao.hlsli, SsaoPixelShader.hlsl, SsaoBlurPixelShader.hlsl.
//
// 디퍼드 프레임에서만 돎 — G-buffer 노멀(GB1)이 있어야 하고, 깊이가 G-buffer 패스에서 이미 다 채워져 있어야 함
// (포워드 프레임은 깊이와 셰이딩이 한 패스라 그 시점에 깊이가 없음. 깊이 프리패스를 따로 두지 않았음).

bool Renderer::CreateSsaoTargets(ViewTarget& view, const std::string& base)
{
	const char* suffixes[2] = { "_SsaoRaw", "_SsaoBlur" };
	TextureHandle* targets[2] = { &view.ssaoRaw, &view.ssaoBlur };
	for (uint32_t i = 0; i < 2; ++i)
	{
		const std::string name = base + suffixes[i];
		*targets[i] = m_device->CreateTexture(TextureDesc{
			.width = view.width,
			.height = view.height,
			.format = Format::R8_UNORM,   // AO 는 0..1 한 채널. 8비트면 충분 (블러가 계단을 지움)
			.mipLevels = 1,
			.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource,
			.debugName = name.c_str(),
		});
		if (!targets[i]->IsValid())
		{
			Log::Error("SSAO 타깃 생성 실패 (%s). 이 뷰는 SSAO 없이 그림.", name.c_str());
			DestroySsaoTargets(view);
			return false;
		}
	}

	// 두 셋은 t29 만 다름: SSAO 패스는 입력 AO 가 없어 흰색, 블러 패스는 ssaoRaw.
	// 순서 = CreateLayouts 의 ssaoLayout: b0, b12, t23 깊이, t26 GB1 노멀, t29.
	const auto makeSet = [&](TextureHandle input, const char* suffix)
	{
		const std::string name = "Set" + std::string(suffix) + "_" + base;
		return m_device->CreateResourceSet(ResourceSetDesc{
			.layout = m_layouts[Layout_Ssao],
			.bindings = { { .buffer = m_perFrameCB }, { .buffer = m_ssaoCB }, { .texture = view.depth }, { .texture = view.gbuffer[1] }, { .texture = input } },
			.debugName = name.c_str(),
		});
	};
	view.ssaoSet = makeSet(m_textures.GetWhite(), "_Ssao");
	view.ssaoBlurSet = makeSet(view.ssaoRaw, "_SsaoBlur");
	if (!view.ssaoSet.IsValid() || !view.ssaoBlurSet.IsValid())
	{
		Log::Error("SSAO 셋 생성 실패 (%s). 이 뷰는 SSAO 없이 그림.", base.c_str());
		DestroySsaoTargets(view);
		return false;
	}
	return true;
}

void Renderer::DestroySsaoTargets(ViewTarget& view)
{
	if (m_device == nullptr) return;
	for (ResourceSetHandle* set : { &view.ssaoSet, &view.ssaoBlurSet })
	{
		if (set->IsValid()) m_device->DestroyResourceSet(*set);
		*set = ResourceSetHandle{};
	}
	for (TextureHandle* texture : { &view.ssaoRaw, &view.ssaoBlur })
	{
		if (texture->IsValid()) m_device->DestroyTexture(*texture);
		*texture = TextureHandle{};
	}
}

void Renderer::UploadSsaoConstants(const SsaoSettings& settings)
{
	const uint32_t sampleCount = static_cast<uint32_t>(std::clamp(settings.sampleCount, 1, static_cast<int>(kMaxSsaoSamples)));

	// 반구 커널 (탄젠트 공간, z = 노멀 쪽). 샘플 수가 바뀔 때만 다시 만듦. 시드를 고정해 실행마다 같은 커널 (스크린샷 비교).
	//   (1) 방향: x, y ∈ [−1, 1], z ∈ [0, 1] 를 정규화 → 반구 위의 방향
	//   (2) 길이: lerp(0.2, 1, (i + u) / n), u 는 [0, 1) 난수 — 층화(stratified). 샘플 i 의 길이가 [i/n, (i+1)/n) 칸 안에
	//       하나씩 들어가 반경 0.2 ~ 1 사이에 고르게 퍼짐 (평균 약 0.6 × 반경). 길이가 반경 방향으로 고르면 부피 밀도는 1/r² 라
	//       중심 가까이에 샘플이 저절로 더 몰림 — 가까운 가림(접촉면)에 무게가 실리면서도 반경 끝까지 닿음.
	//       예전 식(균등 난수 × lerp(0.1, 1, (i/n)²))은 두 번 몰려 평균 길이가 반경의 약 16% 였음 → 반경 0.6 에서 실제로는
	//       0.1 m 안만 봐서, 쌓인 구 사이 틈처럼 가림 물체가 0.2~0.5 m 떨어진 곳이 거의 가려지지 않았음 (SsaoBalls.json).
	if (m_ssaoKernel.size() != sampleCount)
	{
		m_ssaoKernel.resize(sampleCount);
		std::mt19937 random(1337u);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		for (uint32_t i = 0; i < sampleCount; ++i)
		{
			XMFLOAT3 p(unit(random) * 2.0f - 1.0f, unit(random) * 2.0f - 1.0f, unit(random));
			XMVECTOR v = XMVector3Normalize(XMLoadFloat3(&p));
			if (XMVectorGetX(XMVector3LengthSq(v)) < 1.0e-6f) v = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);   // (0,0,0) 이 나온 경우
			const float t = (static_cast<float>(i) + unit(random)) / static_cast<float>(sampleCount);
			v = XMVectorScale(v, 0.2f + 0.8f * t);
			XMStoreFloat4(&m_ssaoKernel[i], v);
		}
	}

	SsaoConstants constants = {};
	constants.radius = (std::max)(settings.radius, 0.001f);
	constants.bias = (std::max)(settings.bias, 0.0f);
	constants.intensity = (std::max)(settings.intensity, 0.0f);
	constants.power = (std::max)(settings.power, 0.01f);
	constants.sampleCount = sampleCount;
	std::copy(m_ssaoKernel.begin(), m_ssaoKernel.end(), constants.kernel);
	// Dynamic CB 라 SSAO 를 꺼도 프레임마다 올림 (D3D12: 그 프레임에 쓰지 않은 Dynamic 버퍼는 바인딩할 수 없음).
	m_device->UpdateBuffer(m_ssaoCB, &constants, sizeof(constants));
}

void Renderer::RenderSsao(RHI::CommandList& cmd, ViewTarget& view)
{
	// 들어올 때: 깊이 DepthRead, GB1 ShaderResource (RenderDeferredScene 이 맞춰 둠).
	const auto fullscreen = [&](TextureHandle target, ResourceSetHandle set, bool blur, const char* name)
	{
		cmd.Barrier(target, ResourceState::RenderTarget);
		const RenderPassDesc pass{
			.colors = { { .texture = target, .load = LoadOp::DontCare } },   // 모든 픽셀을 덮어씀
			.colorCount = 1,
			.debugName = name,
		};
		cmd.BeginRenderPass(pass);
		const PipelineHandle pipeline = GetSsaoPipeline(blur);
		if (pipeline.IsValid())
		{
			cmd.SetPipelineState(pipeline);
			cmd.SetResourceSet(kSetSsao, set);
			cmd.Draw(3, 0);
			++m_stats.pipelineSwitches;
			++m_stats.draws;
			m_stats.triangles += 1;
		}
		cmd.EndRenderPass();
		cmd.Barrier(target, ResourceState::ShaderResource);
	};
	fullscreen(view.ssaoRaw, view.ssaoSet, false, "SsaoRaw");
	fullscreen(view.ssaoBlur, view.ssaoBlurSet, true, "SsaoBlur");
}

PipelineHandle Renderer::GetSsaoPipeline(bool blur)
{
	PipelineHandle& cached = m_ssaoPipelines[blur ? 1 : 0];
	if (cached.IsValid()) return cached;
	PipelineStateDesc desc = m_passDesc[blur ? Pass_SsaoBlur : Pass_Ssao];
	desc.rtvFormats[0] = Format::R8_UNORM;
	desc.rtvCount = 1;
	cached = m_device->CreatePipeline(desc);
	return cached;
}

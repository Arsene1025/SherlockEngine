#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "Graphics/ShaderConstants.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Scene/GameObject.h"
#include "Scene/Rain.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>

// 비 (Renderer 의 멤버 함수를 파일만 나눠 구현함). 설계: docs/plan/rain-plan.html.
//
// 프레임 안에서 비가 지나는 길:
//   Prepare            PrepareRain: 첫 번째 Rain → 빗방울 수·젖음 보간·차폐 맵 위치 (텍셀 스냅) → b0 의 rain* 칸 (UploadPerFrameConstants)
//   BuildDrawList      그림자를 만드는 항목마다 차폐 맵 PSO (그림자 PSO 의 바이어스 없는 변형, DrawItem::occluderPipeline)
//   ShadowPass 다음    RenderRainOcclusionPass: 카메라 위에서 바로 아래로 직교 투영한 깊이 → m_rainMap (프레임 셋 t24)
//   G-buffer / 포워드  재질 PS 의 ApplyWetness (MaterialSurface.hlsli) 가 t24 로 "비를 맞는 자리" 만 젖게 함
//   씬 패스 두 번째    DrawRain: 빗줄기 (Pass_Rain) 와 튐 (Pass_RainSplash). 정점 버퍼 없이 SV_VertexID 로 위치를 계산 — 상태 없는
//                      절차적 파티클이라 시뮬레이션 패스가 없음 (빗방울은 종단 속도로 떨어지므로 위치 = 시작 + 속도 × 시간)

using namespace DirectX;

namespace
{
	constexpr float kMapHeightAbove = 100.0f;   // 차폐 맵 윗면 = 카메라 y (1 m 로 내림) + 이 값. 이보다 높은 지붕은 차폐로 보지 않음
	constexpr float kMapDepthRange = 200.0f;    // 윗면에서 아래로 이 거리까지. 그 아래는 "열린 하늘" (깊이 1 = 아무것도 없음)
	constexpr float kSplashPeriod = 0.35f;      // 튐 하나의 수명 (초). 칸의 빗방울 주기 1 / 1.3 초보다 짧아야 함
	constexpr float kImpactCell = 0.35f;        // 빗방울이 닿는 칸 크기 (m). Shaders/Rain.hlsli 의 kRainImpactCell 과 같아야 함
	constexpr uint32_t kMaxSplashGrid = 256;    // 튐 격자 한 변의 최대 칸 수 (겹 둘 → 최대 13만 개)
	constexpr float kSplashSize = 0.12f;        // 튐 크기 (m)
	constexpr float kSoftDistance = 0.3f;       // 빗줄기가 장면 깊이와 만나는 곳을 흐리게 하는 거리 (m)
	constexpr float kReferenceDrops = 20000.0f; // maxDrops 기본값. 빗방울이 닿는 칸 비율(물결·튐)은 이 수에 대한 빗방울 비율(최대 1)을 곱함
	constexpr double kRainTimeWrap = 1000.0;    // b0 rainParams.w (물결 시간) 를 감는 주기 (초)

	// 512·1024·2048 중 가장 가까운 것 (텍스처를 아무 크기로 만들지 않게).
	uint32_t SnapResolution(int value)
	{
		if (value <= 768) return 512;
		if (value <= 1536) return 1024;
		return 2048;
	}
}

void Renderer::PrepareRain(const Scene& scene, const Camera& camera, float totalTime)
{
	const float dt = std::clamp(totalTime - m_rainLastTime, 0.0f, 0.25f);   // 젖음 보간 (일시정지면 0, 긴 멈춤 뒤에는 자름)
	m_rainLastTime = totalTime;
	m_frameRain = FrameRain{};

	// 첫 번째 켜진 Rain (계층과 상관없음 — 위치를 쓰지 않으므로 씬의 평평한 목록을 그대로 훑음).
	const Rain* rain = nullptr;
	uint64_t rainObject = 0;
	for (const auto& objectPtr : scene.GetObjects())
	{
		for (const auto& behaviour : objectPtr->GetBehaviours())
		{
			const Rain* candidate = dynamic_cast<const Rain*>(behaviour.get());
			if (candidate == nullptr || !candidate->enabled) continue;
			if (rain == nullptr)
			{
				rain = candidate;
				rainObject = objectPtr->GetId();
			}
			else if (!m_rainExtraWarned)
			{
				m_rainExtraWarned = true;
				Log::Warn("Rain: 비가 둘 이상임. 첫 번째 것만 쓰고 '%s' 의 Rain 은 무시함.", objectPtr->GetName().c_str());
			}
		}
	}
	if (rain == nullptr)
	{
		m_rainPrevObject = 0;
		return;
	}

	FrameRain& frame = m_frameRain;
	frame.active = true;
	frame.objectId = rainObject;

	// ---- 시간과 젖음 ----
	// --rain-time 이면 시간 고정, 젖음은 바로 목표값 (스크린샷이 실행마다 같음). 씬 불러오기·재생 전환·Rain 이 새로 생긴 프레임도 바로
	// 목표값 — 천천히 젖고 마르는 것은 같은 Rain 의 세기를 바꿀 때만.
	const bool fixedTime = m_settings.rainTimeOverride >= 0.0f;
	const double time = fixedTime ? static_cast<double>(m_settings.rainTimeOverride) : static_cast<double>(totalTime);
	const float intensity = std::clamp(rain->intensity, 0.0f, 1.0f);
	const float wetTarget = std::clamp(rain->wetness, 0.0f, 1.0f) * intensity;
	if (m_rainSnapWetness || fixedTime || m_rainPrevObject != rainObject)
	{
		m_rainWetness = wetTarget;
	}
	else
	{
		// 지수 보간: tau 초 동안 남은 차이의 63% 를 따라감. 젖을 때와 마를 때 tau 가 다름.
		const float tau = wetTarget > m_rainWetness ? rain->wetTime : rain->dryTime;
		const float k = tau > 0.0f ? 1.0f - std::exp(-dt / tau) : 1.0f;
		m_rainWetness += (wetTarget - m_rainWetness) * k;
	}
	m_rainSnapWetness = false;
	m_rainPrevObject = rainObject;

	// ---- 차폐 맵: 카메라 xz 중심, 텍셀 크기로 스냅 (카메라가 움직여도 지붕 가장자리가 지글거리지 않게) ----
	const uint32_t resolution = SnapResolution(rain->occlusionResolution);
	const float size = (std::max)(rain->occlusionSize, 4.0f);
	const float texel = size / static_cast<float>(resolution);
	const XMFLOAT3 eye = camera.GetPosition();
	const float centerX = std::round(eye.x / texel) * texel;
	const float centerZ = std::round(eye.z / texel) * texel;
	const float top = std::floor(eye.y) + kMapHeightAbove;
	// 바로 아래를 보는 직교 카메라. LH 에서 up = +Z 면 뷰의 +x = 월드 +X, 뷰의 +y = 월드 +Z → 맵 u = (x − minX) / size, v = 1 − (z − minZ) / size.
	// 깊이는 직교라 선형: 월드 y = top − depth × range.
	const XMMATRIX mapView = XMMatrixLookToLH(XMVectorSet(centerX, top, centerZ, 1.0f), XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f), XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
	const XMMATRIX mapProj = XMMatrixOrthographicLH(size, size, 0.0f, kMapDepthRange);
	XMStoreFloat4x4(&frame.mapViewProj, mapView * mapProj);

	// 맵 텍스처 (해상도가 바뀌면 다시 만듦). Render 가 프레임 셋을 꽂기 전에 ShaderResource 로 돌려 둠 (D3D11 은 SRV 를 꽂을 때 상태를 검사함).
	if (m_rainMapResolution != resolution || !m_rainMap.IsValid())
	{
		m_device->DestroyTexture(m_rainMap);
		m_frameSetRainMap = TextureHandle{};   // 핸들 값이 다시 쓰여도 프레임 셋을 새 뷰로 다시 만들게
		m_rainMap = m_device->CreateTexture(TextureDesc{
			.width = resolution,
			.height = resolution,
			.format = Format::D32_FLOAT,
			.mipLevels = 1,
			.bindFlags = TextureBind_DepthStencil | TextureBind_ShaderResource,
			.debugName = "RainOcclusionMap",
		});
		m_rainMapResolution = m_rainMap.IsValid() ? resolution : 0;
		if (!m_rainMap.IsValid())
		{
			Log::Error("Rain: 차폐 맵 생성 실패 (%u²). 지붕 아래도 비가 내림.", resolution);
		}
	}
	const bool hasMap = m_rainMap.IsValid();

	// ---- b0 칸 ----
	frame.rainParams = XMFLOAT4(intensity, m_rainWetness, hasMap ? 1.0f : 0.0f, static_cast<float>(std::fmod(time, kRainTimeWrap)));
	frame.rainMapXZ = XMFLOAT4(centerX - 0.5f * size, centerZ - 0.5f * size, 1.0f / size, texel);
	// 높이 비교 여유: 텍셀 두 개 + 5 cm. 경사 지붕은 텍셀 안에서도 높이가 바뀌고, 젖음은 이 위치에서 맵을 읽은 그 면 자체가 "가장 위" 임.
	frame.rainMapY = XMFLOAT4(top, kMapDepthRange, 2.0f * texel + 0.05f, static_cast<float>(resolution));
	// 빗방울이 적으면 바닥을 때리는 빗방울도 적음: 튐 수와 물결이 생기는 칸의 비율을 maxDrops / 기본값 (최대 1) 만큼 줄임.
	// 세기는 링 높이가 아니라 링 수로 반영 (빗방울 하나가 만드는 링의 크기는 같음).
	const float dropRatio = std::clamp(static_cast<float>(rain->maxDrops) / kReferenceDrops, 0.0f, 1.0f);
	frame.rainSurface = XMFLOAT4(rain->ripples ? 1.0f : 0.0f, std::clamp(rain->puddles, 0.0f, 1.0f), dropRatio * intensity, 0.0f);

	// ---- b8: 빗줄기·튐 ----
	RainConstants& c = frame.constants;
	const float radius = (std::max)(rain->areaRadius, 1.0f);
	const float height = (std::max)(rain->areaHeight, 1.0f);
	c.boxSize = XMFLOAT3(2.0f * radius, height, 2.0f * radius);
	c.boxBelow = 1.0f / 3.0f;
	c.velocity = XMFLOAT3(rain->wind.x, -(std::max)(rain->fallSpeed, 0.1f), rain->wind.z);
	// 시간 몫은 축마다 상자 크기로 감아 넘김: 셰이더에서 velocity × t 를 곱하면 t 가 커질수록 float 정밀도가 떨어지고, 시간 하나를
	// "높이 / 낙하 속력" 주기로 감으면 바람이 있는 xz 는 감는 순간 모든 빗방울이 옆으로 한꺼번에 뜀.
	c.fallOffset = XMFLOAT3(static_cast<float>(std::fmod(static_cast<double>(c.velocity.x) * time, static_cast<double>(c.boxSize.x))),
							static_cast<float>(std::fmod(static_cast<double>(c.velocity.y) * time, static_cast<double>(c.boxSize.y))),
							static_cast<float>(std::fmod(static_cast<double>(c.velocity.z) * time, static_cast<double>(c.boxSize.z))));
	c.dropCount = static_cast<uint32_t>(std::lround(std::clamp(rain->maxDrops, 0, 100000) * intensity));
	c.streakLength = (std::max)(rain->streakLength, 0.0f);
	c.streakWidth = (std::max)(rain->streakWidth, 0.0005f);
	const float brightness = (std::max)(rain->brightness, 0.0f);
	c.color = XMFLOAT3(rain->color.x * brightness, rain->color.y * brightness, rain->color.z * brightness);
	c.nearFade = (std::max)(rain->nearFade, 0.01f);
	c.softDistance = kSoftDistance;
	c.seed = static_cast<uint32_t>(rain->seed);
	// 튐은 물결과 같은 칸 (Rain.hlsli 의 RainImpactInCell) 에서 터짐: 카메라 주변 splashRadius 를 덮는 정사각 격자 두 겹의 칸마다 하나씩
	// 그리고, 이번 주기에 빗방울이 떨어지지 않은 칸은 VS 가 버림. 그래서 그리는 수 = 칸 수, 실제로 보이는 수 ≈ 원 안 칸 × rainSurface.z ×
	// (splashPeriod × 1.3).
	c.splashRadius = radius * 0.8f;
	c.splashGrid = (std::min)(static_cast<uint32_t>(std::ceil(2.0f * c.splashRadius / kImpactCell)) + 2u, kMaxSplashGrid);
	const bool splashes = rain->splashes && hasMap && frame.rainSurface.z > 0.0f;
	c.splashCount = splashes ? 2u * c.splashGrid * c.splashGrid : 0u;
	c.splashPeriod = kSplashPeriod;
	c.splashSize = kSplashSize;
	frame.drawRain = c.dropCount > 0 || c.splashCount > 0;

	m_stats.rainActive = true;
	m_stats.rainDrops = c.dropCount;
	m_stats.rainSplashes = c.splashCount;
	m_stats.rainWetness = m_rainWetness;
}

void Renderer::UploadRainConstants()
{
	// 비 셋은 빗줄기·튐 패스만 묶으므로 그릴 때만 올림 (D3D12 Dynamic 상수버퍼 규칙은 "묶기 전에 그 프레임에 한 번 씀").
	if (!m_frameRain.active || !m_frameRain.drawRain) return;
	m_device->UpdateBuffer(m_rainCB, &m_frameRain.constants, sizeof(RainConstants));
}

void Renderer::RenderRainOcclusionPass(RHI::CommandList& cmd)
{
	if (!m_rainMap.IsValid()) return;
	// 프레임 셋은 이 패스 전에 이미 꽂혀 있음 (t24 = 이 맵). D3D11 은 BeginRenderPass 가 깊이 타깃의 SRV 바인딩을 풀고, 다음 씬 패스의
	// DrawItems 가 셋을 다시 꽂음. D3D12 는 테이블이 DepthWrite 상태의 맵을 가리키지만 그림자 VS·알파 PS 는 t24 를 읽지 않음.
	cmd.Barrier(m_rainMap, ResourceState::DepthWrite);
	const RenderPassDesc pass{
		.colorCount = 0,
		.depth = { .texture = m_rainMap, .load = LoadOp::Clear, .clearDepth = 1.0f },   // 1 = 아무것도 없음 (열린 하늘)
		.debugName = "RainOcclusionPass",
	};
	cmd.BeginRenderPass(pass);
	const float resolution = static_cast<float>(m_rainMapResolution);
	cmd.SetViewport(Viewport{ .x = 0.0f, .y = 0.0f, .width = resolution, .height = resolution });
	DepthDrawState state;
	DrawDepthView(cmd, m_frameRain.mapViewProj, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f), false, true, state, m_stats.rainOcclusionDraws, m_stats.rainOcclusionCulled);
	cmd.EndRenderPass();
	cmd.Barrier(m_rainMap, ResourceState::ShaderResource);   // 프레임 셋 t24 (빗줄기 VS, 재질 PS)
}

void Renderer::DrawRain(RHI::CommandList& cmd, const ViewTarget& view)
{
	const RainConstants& c = m_frameRain.constants;
	if (!view.particleDepthSet.IsValid()) return;
	// 셋: 0 = 프레임 (b0 = 이 뷰의 카메라, t24 차폐 맵), 1 = 비 (b8), 2 = 이 뷰의 깊이 (t23, 소프트 페이드). 셋 번호가 파티클 패스와 같음.
	cmd.SetResourceSet(kSetFrame, m_frameSet);
	cmd.SetResourceSet(kSetParticle, m_rainSet);
	cmd.SetResourceSet(kSetParticleDepth, view.particleDepthSet);
	m_stats.resourceSetSwitches += 3;
	if (c.dropCount > 0)
	{
		const PipelineHandle pipeline = GetRainPipeline(false);
		if (pipeline.IsValid())
		{
			cmd.SetPipelineState(pipeline);
			++m_stats.pipelineSwitches;
			cmd.Draw(c.dropCount * 6u, 0);   // 빗방울 하나 = 쿼드 하나 = 정점 6 개. 정점 버퍼 없음
			++m_stats.draws;
		}
	}
	if (c.splashCount > 0)
	{
		const PipelineHandle pipeline = GetRainPipeline(true);
		if (pipeline.IsValid())
		{
			cmd.SetPipelineState(pipeline);
			++m_stats.pipelineSwitches;
			cmd.Draw(c.splashCount * 6u, 0);
			++m_stats.draws;
		}
	}
}

PipelineHandle Renderer::GetRainPipeline(bool splash)
{
	PipelineHandle& cached = splash ? m_rainSplashPipeline : m_rainPipeline;
	if (cached.IsValid()) return cached;
	cached = m_device->CreatePipeline(m_passDesc[splash ? Pass_RainSplash : Pass_Rain]);   // 타깃 포맷까지 BuildPassDescs 가 채움
	return cached;
}

void Renderer::DestroyRainResources()
{
	if (m_device == nullptr) return;
	m_device->DestroyTexture(m_rainMap);
	m_rainMap = TextureHandle{};
	m_rainMapResolution = 0;
	m_device->DestroyResourceSet(m_rainSet);
	m_rainSet = ResourceSetHandle{};
}

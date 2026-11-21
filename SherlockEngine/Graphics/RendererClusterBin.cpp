#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/ResourceDesc.h"
#include "Graphics/ShaderConstants.h"
#include "Scene/Camera.h"
#include "Core/Log.h"
#include "Core/Profiler.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace DirectX;   // 이 파일 안에서만

// GPU 클러스터 비닝 (Renderer 의 멤버 함수를 파일만 나눠 구현함). 설계·검증·읽는 순서: docs/plan/gpu-cluster-binning-record.html.
// CPU 비닝(RendererClusters.cpp 의 BuildClusters)과 같은 결과 — t30 (상자마다 시작, 개수), t31 (조명 번호, 상자 안 오름차순) —
// 를 컴퓨트 셰이더 다섯 개로 만듦 (Shaders/ClusterBin.hlsli).
//
//   PrepareGpuClusters        설정·파이프라인·버퍼 확인. 조명 버퍼 용량이 바뀌면 용량에 묶인 버퍼와 셋을 다시 만듦
//   UploadLightBounds         t7: CullLights 가 만든 m_lightBounds (월드 경계 구) 를 그대로 — t15 와 같은 번호
//   UploadClusterBinConstants b13: 뷰 행렬의 열, 투영 항, 슬라이스 경계 z (CPU 와 같은 std::pow 값)
//   DispatchClusterBinning    View → Tile (1단계) → Count → Scan → Fill (2단계). 끝나면 t30·t31 을 ShaderResource 로
//   ReadGpuClusterStats       몇 프레임 (≥ kFrameCount) 전 메인 뷰의 통계를 Staging 링에서 읽음 (기다리지 않음)
//
// CPU 경로와 다른 점: 1단계(타일)는 타일 절두체의 옆면 평면으로 거름 (CPU 는 경계 구를 감싼 상자의 NDC 투영). 둘 다 보수적이라
// 픽셀이 받는 조명은 같고, (상자, 조명) 쌍 수는 조금 다를 수 있음 (빛이 0 인 조명이 목록에 더 있거나 덜 있음).

namespace
{
	constexpr uint32_t kBinGroupSize = 64;                        // View·Tile·Count·Fill 의 [numthreads(64, 1, 1)]
	constexpr uint64_t kMaxTileMaskBytes = 64ull * 1024 * 1024;   // u2 (144 × ⌈조명 용량 / 32⌉ × 4) 상한 — 조명 약 1,170 만 개. 넘으면 CPU 경로

	uint32_t MaskWords(uint32_t lights)
	{
		return (lights + 31) / 32;
	}   // ClusterBin.hlsli 의 BinWordCount 와 같은 식

	uint32_t GroupsFor(uint32_t count)
	{
		return (std::max)(1u, (count + kBinGroupSize - 1) / kBinGroupSize);
	}
}

void Renderer::DestroyGpuClusterResources()
{
	for (ResourceSetHandle& set : m_clusterBinSet)
	{
		m_device->DestroyResourceSet(set);
		set = ResourceSetHandle{};
	}
	for (BufferHandle* buffer : { &m_clusterBinCB[0], &m_clusterBinCB[1], &m_lightBoundsBuffer, &m_clusterViewBounds, &m_clusterTileCounts, &m_clusterTileMasks,
								  &m_clusterGridGpu, &m_clusterIndexGpu, &m_clusterStatsBuffer, &m_clusterStatsReadback[0], &m_clusterStatsReadback[1], &m_clusterStatsReadback[2], &m_clusterStatsReadback[3] })
	{
		m_device->DestroyBuffer(*buffer);
		*buffer = BufferHandle{};
	}
	static_assert(kClusterViews == 2 && kClusterStatsSlots == 4, "위 목록의 [0] … [3]");
	m_clusterStatsCursor = 0;
	m_clusterGpuLightCapacity = 0;
	for (bool& pending : m_clusterStatsPending)
	{
		pending = false;
	}
}

bool Renderer::PrepareGpuClusters()
{
	if (!m_settings.clusterGpu || m_clusterGpuFailed) return false;
	for (uint32_t pass = ComputePass_ClusterView; pass <= ComputePass_ClusterFill; ++pass)
	{
		if (!GetComputePipeline(static_cast<ComputePassId>(pass)).IsValid()) return false;   // 컴퓨트 미지원·셰이더 오류 → CPU 경로
	}

	const auto fail = [this](const char* what)
	{
		Log::Error("GPU 클러스터 비닝 : %s 생성 실패. CPU 비닝으로 돌아감.", what);
		DestroyGpuClusterResources();
		m_clusterGpuFailed = true;
		return false;
	};

	// ---- 조명 수와 무관한 것: 처음 한 번 ----
	if (!m_clusterGridGpu.IsValid())
	{
		for (uint32_t v = 0; v < kClusterViews; ++v)
		{
			m_clusterBinCB[v] = m_device->CreateBuffer(BufferDesc{ .size = sizeof(ClusterBinConstants), .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_Constant, .debugName = v == 0 ? "CB_ClusterBin" : "CB_ClusterBinPreview" });
		}
		const auto gpuBuffer = [this](uint32_t count, uint32_t stride, const char* name)
		{
			return m_device->CreateBuffer(BufferDesc{ .size = count * stride, .usage = BufferUsage::Default, .bindFlags = BufferBind_ShaderResource | BufferBind_UnorderedAccess, .stride = stride, .debugName = name });
		};
		m_clusterGridGpu = gpuBuffer(kClusterCount, 2 * sizeof(uint32_t), "SB_ClusterGridGpu");
		// t31 은 상한 크기로 한 번 (4 MiB). CPU 경로처럼 "세고 나서 키우기"를 하려면 개수를 CPU 로 읽고 기다려야 함.
		m_clusterIndexGpu = gpuBuffer(kMaxClusterLightRefs, sizeof(uint32_t), "SB_ClusterLightsGpu");
		m_clusterTileCounts = gpuBuffer(kClusterTileCount, sizeof(uint32_t), "SB_ClusterTileCounts");
		m_clusterStatsBuffer = gpuBuffer(kClusterStatCount, sizeof(uint32_t), "SB_ClusterStats");
		for (uint32_t i = 0; i < kClusterStatsSlots; ++i)
		{
			m_clusterStatsReadback[i] = m_device->CreateBuffer(BufferDesc{ .size = kClusterStatCount * static_cast<uint32_t>(sizeof(uint32_t)),
																		   .usage = BufferUsage::Staging,
																		   .bindFlags = 0,
																		   .stride = sizeof(uint32_t),
																		   .debugName = "RB_ClusterStats" });
			if (!m_clusterStatsReadback[i].IsValid()) return fail("통계 Staging 버퍼");
		}
		if (!m_clusterBinCB[0].IsValid() || !m_clusterBinCB[1].IsValid() || !m_clusterGridGpu.IsValid() || !m_clusterIndexGpu.IsValid() ||
			!m_clusterTileCounts.IsValid() || !m_clusterStatsBuffer.IsValid())
		{
			return fail("버퍼");
		}
		Log::Info("GPU 클러스터 비닝 : t30·t31 Default 버퍼 (%u + %u 바이트)", kClusterCount * 8, kMaxClusterLightRefs * 4);
	}

	// ---- 조명 버퍼 용량에 묶인 것: t7, u0, u2. 용량이 바뀌면 다시 만들고 셋도 다시 만듦 ----
	if (m_clusterGpuLightCapacity != m_lightCapacity)
	{
		const uint32_t capacity = m_lightCapacity;
		const uint64_t tileBytes = static_cast<uint64_t>(kClusterTileCount) * MaskWords(capacity) * sizeof(uint32_t);
		if (tileBytes > kMaxTileMaskBytes)
		{
			Log::Error("GPU 클러스터 비닝 : 조명 %u 개 용량의 타일 마스크가 %llu MiB 로 상한을 넘음. CPU 비닝으로 돌아감.", capacity, tileBytes >> 20);
			DestroyGpuClusterResources();
			m_clusterGpuFailed = true;
			return false;
		}
		for (ResourceSetHandle& set : m_clusterBinSet)
		{
			m_device->DestroyResourceSet(set);
			set = ResourceSetHandle{};
		}
		for (BufferHandle* buffer : { &m_lightBoundsBuffer, &m_clusterViewBounds, &m_clusterTileMasks })
		{
			m_device->DestroyBuffer(*buffer);
			*buffer = BufferHandle{};
		}
		m_lightBoundsBuffer = m_device->CreateBuffer(BufferDesc{ .size = capacity * static_cast<uint32_t>(sizeof(XMFLOAT4)), .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_ShaderResource, .stride = sizeof(XMFLOAT4), .debugName = "SB_LightBounds" });
		m_clusterViewBounds = m_device->CreateBuffer(BufferDesc{ .size = capacity * static_cast<uint32_t>(sizeof(XMFLOAT4)), .usage = BufferUsage::Default, .bindFlags = BufferBind_ShaderResource | BufferBind_UnorderedAccess, .stride = sizeof(XMFLOAT4), .debugName = "SB_ClusterViewBounds" });
		m_clusterTileMasks = m_device->CreateBuffer(BufferDesc{ .size = static_cast<uint32_t>(tileBytes), .usage = BufferUsage::Default, .bindFlags = BufferBind_ShaderResource | BufferBind_UnorderedAccess, .stride = sizeof(uint32_t), .debugName = "SB_ClusterTileMasks" });
		if (!m_lightBoundsBuffer.IsValid() || !m_clusterViewBounds.IsValid() || !m_clusterTileMasks.IsValid()) return fail("조명 용량 버퍼");

		// 셋의 bindings 순서 = CreateLayouts 의 clusterBinLayout.Add 순서: b13, t7, u0 … u5.
		for (uint32_t v = 0; v < kClusterViews; ++v)
		{
			m_clusterBinSet[v] = m_device->CreateResourceSet(ResourceSetDesc{
				.layout = m_layouts[Layout_ClusterBin],
				.bindings = { { .buffer = m_clusterBinCB[v] }, { .buffer = m_lightBoundsBuffer }, { .buffer = m_clusterViewBounds }, { .buffer = m_clusterTileCounts }, { .buffer = m_clusterTileMasks }, { .buffer = m_clusterGridGpu }, { .buffer = m_clusterIndexGpu }, { .buffer = m_clusterStatsBuffer } },
				.debugName = v == 0 ? "Set_ClusterBin" : "Set_ClusterBinPreview",
			});
			if (!m_clusterBinSet[v].IsValid()) return fail("ResourceSet");
		}
		m_clusterGpuLightCapacity = capacity;
		Log::Info("GPU 클러스터 비닝 : 조명 용량 %u (타일 마스크 %llu KiB)", capacity, tileBytes >> 10);
	}
	return true;
}

void Renderer::UploadLightBounds()
{
	// m_lightBounds 는 CullLights 가 조명마다 채움 (방향광 w < 0, 범위 0 이면 w = 0). 셰이더는 w ≤ 0 을 건너뜀.
	// D3D12 의 Dynamic 버퍼는 프레임마다 써야 바인딩됨 — 조명이 없어도 한 칸 올림.
	const uint32_t count = (std::min)(static_cast<uint32_t>(m_lightBounds.size()), m_clusterGpuLightCapacity);
	static const XMFLOAT4 kNoBounds(0.0f, 0.0f, 0.0f, 0.0f);
	m_device->UpdateBuffer(m_lightBoundsBuffer, count > 0 ? m_lightBounds.data() : &kNoBounds, (std::max)(count, 1u) * static_cast<uint32_t>(sizeof(XMFLOAT4)));
}

void Renderer::UploadClusterBinConstants(const Camera& camera, uint32_t viewIndex)
{
	ClusterBinConstants c = {};
	XMFLOAT4X4 view, proj;
	XMStoreFloat4x4(&view, camera.GetViewMatrix());
	XMStoreFloat4x4(&proj, camera.GetProjectionMatrix());
	// 행 벡터 관례: p_view = (p, 1) · V → x = p.x·V11 + p.y·V21 + p.z·V31 + V41 (V 의 열 0).
	c.viewColumn[0] = XMFLOAT4(view._11, view._21, view._31, view._41);
	c.viewColumn[1] = XMFLOAT4(view._12, view._22, view._32, view._42);
	c.viewColumn[2] = XMFLOAT4(view._13, view._23, view._33, view._43);
	c.projScale = XMFLOAT4(proj._11, proj._22, proj._31, proj._32);
	const float nearZ = camera.GetNearZ();
	const float farZ = camera.GetFarZ();
	c.projOffset = XMFLOAT4(proj._41, proj._42, nearZ, farZ);
	// 슬라이스 경계 — BuildClusters (1) 과 같은 식·같은 float 연산.
	const bool perspective = camera.IsUsePerspectiveProjection();
	float* sliceZ = &c.sliceZ[0].x;
	for (uint32_t k = 0; k <= kClusterGridZ; ++k)
	{
		const float t = static_cast<float>(k) / static_cast<float>(kClusterGridZ);
		sliceZ[k] = perspective ? nearZ * std::pow(farZ / nearZ, t) : nearZ + (farZ - nearZ) * t;
	}
	c.lightCount = (std::min)(static_cast<uint32_t>(m_lightBounds.size()), m_clusterGpuLightCapacity);
	c.tileStride = MaskWords(m_clusterGpuLightCapacity);
	c.perspective = perspective ? 1u : 0u;
	c.maxRefs = kMaxClusterLightRefs;
	c.useTiles = m_settings.clusterGpuTiles ? 1u : 0u;
	m_device->UpdateBuffer(m_clusterBinCB[viewIndex], &c, sizeof(c));
}

void Renderer::ReadGpuClusterStats()
{
	// 복사한 순서대로 (가장 오래된 슬롯부터) 읽을 수 있는 만큼 읽고, 가장 최근에 읽은 값을 통계로 씀. 슬롯은 링이라
	// 지금 커서 위치가 가장 오래된 복사임. kFrameCount 프레임이 지난 복사만 읽음 — D3D12 는 BeginFrame 이 그 프레임을 이미
	// 기다렸으므로 끝나 있음 (ReadStagingBuffer 는 D3D12 에서 완료를 확인하지 않음). D3D11 은 끝나지 않았으면 false — 그 슬롯과
	// 뒤의 슬롯은 다음 프레임에 다시 봄.
	++m_clusterBinFrame;
	static_assert(kClusterStatValues == kClusterStatCount, "Renderer.h 의 kClusterStatValues 와 ShaderConstants.h 의 kClusterStatCount");
	for (uint32_t k = 0; k < kClusterStatsSlots; ++k)
	{
		const uint32_t slot = (m_clusterStatsCursor + k) % kClusterStatsSlots;
		if (!m_clusterStatsPending[slot]) continue;
		if (m_clusterBinFrame - m_clusterStatsCopyFrame[slot] < kFrameCount) break;   // 더 최근 복사 — 아직 끝났다는 보장이 없음
		uint32_t values[kClusterStatCount] = {};
		if (!m_device->ReadStagingBuffer(m_clusterStatsReadback[slot], values, sizeof(values))) break;   // 아직 GPU 가 복사 중 — 뒤의 것도 기다림
		std::memcpy(m_clusterGpuStats, values, sizeof(values));
		m_clusterStatsPending[slot] = false;
	}
	m_stats.clusterLightRefs = m_clusterGpuStats[ClusterStat_Refs];
	m_stats.clusterMaxLights = m_clusterGpuStats[ClusterStat_MaxLights];
	m_stats.clusterOverflow = m_clusterGpuStats[ClusterStat_Overflow];
	m_stats.clusterTileRefs = m_clusterGpuStats[ClusterStat_TileRefs];
	m_stats.clusterTileMaxLights = m_clusterGpuStats[ClusterStat_TileMaxLights];
}

void Renderer::DispatchClusterBinning(RHI::CommandList& cmd, uint32_t viewIndex)
{
	if (m_settings.clustered)
	{
		// 버퍼 상태는 프레임마다 Common 으로 돌아감 → 쓰기 전에 매번 UnorderedAccess (D3D11 은 이때 SRV 바인딩을 풂).
		const BufferHandle uavs[] = { m_clusterViewBounds, m_clusterTileCounts, m_clusterTileMasks, m_clusterGridGpu, m_clusterIndexGpu, m_clusterStatsBuffer };
		for (const BufferHandle& buffer : uavs)
		{
			cmd.Barrier(buffer, ResourceState::UnorderedAccess);
		}

		const uint32_t lightCount = (std::min)(static_cast<uint32_t>(m_lightBounds.size()), m_clusterGpuLightCapacity);
		// 단계마다 GPU 구간을 남김 (프로파일러 창·--avg-frames 의 gpu:Bin*). 메인 뷰만 — 미리보기는 바깥 PreviewClusterBin 하나.
		const auto run = [&](ComputePassId pass, uint32_t groups, const char* name, uint32_t groupsY = 1)
		{
			if (viewIndex == 0)
			{
				Profiler::BeginGpu(cmd, name);
			}
			cmd.SetComputePipeline(GetComputePipeline(pass));
			cmd.SetComputeResourceSet(kSetClusterBin, m_clusterBinSet[viewIndex]);
			cmd.Dispatch(groups, groupsY, 1);
			if (viewIndex == 0)
			{
				Profiler::EndGpu(cmd);
			}
		};
		// 1. 경계 구 → 뷰 공간 (+ 통계 칸 비우기)
		run(ComputePass_ClusterView, GroupsFor(lightCount), "BinView");
		cmd.UavBarrier(m_clusterViewBounds);
		cmd.UavBarrier(m_clusterStatsBuffer);
		// 2. 1단계: (타일 144 개 × 조명 32 개 워드) → 타일 비트마스크. 끄면 상자가 조명 전체를 봄 — 비교 측정용 --cluster-tiles=0
		if (m_settings.clusterGpuTiles)
		{
			run(ComputePass_ClusterTile, GroupsFor(MaskWords(lightCount)), "BinTile", kClusterTileCount);
			cmd.UavBarrier(m_clusterTileMasks);
		}
		// 3. 2단계 세기: 상자 3456 개 (z = 0 상자는 타일 통계도 — u1)
		run(ComputePass_ClusterCount, GroupsFor(kClusterCount), "BinCount");
		cmd.UavBarrier(m_clusterGridGpu);
		cmd.UavBarrier(m_clusterTileCounts);
		// 4. 누적합 (그룹 하나)
		run(ComputePass_ClusterScan, 1, "BinScan");
		cmd.UavBarrier(m_clusterGridGpu);
		cmd.UavBarrier(m_clusterStatsBuffer);
		// 5. 2단계 채우기
		run(ComputePass_ClusterFill, GroupsFor(kClusterCount), "BinFill");
	}

	// 그리기 패스가 t30·t31 로 읽음. 클러스터드가 꺼진 프레임에도 프레임 셋이 이 버퍼를 가리키므로 상태를 맞춰 둠
	// (셰이더는 clusterParams.x 를 보고 읽지 않음).
	cmd.Barrier(m_clusterGridGpu, ResourceState::ShaderResource);
	cmd.Barrier(m_clusterIndexGpu, ResourceState::ShaderResource);

	if (viewIndex == 0 && m_settings.clustered)
	{
		// 통계 → Staging (복사는 Common 에서. ReadGpuClusterStats 가 몇 프레임 뒤에 읽음). 아직 읽지 못한 슬롯이면 이번 프레임은
		// 건너뜀 — 덮어쓰면 D3D11 에서 CPU 가 읽는 중인 값과 섞이거나, 읽기가 계속 밀림.
		const uint32_t slot = m_clusterStatsCursor;
		if (!m_clusterStatsPending[slot])
		{
			cmd.Barrier(m_clusterStatsBuffer, ResourceState::Common);
			cmd.CopyBuffer(m_clusterStatsReadback[slot], m_clusterStatsBuffer);
			m_clusterStatsPending[slot] = true;
			m_clusterStatsCopyFrame[slot] = m_clusterBinFrame;
			m_clusterStatsCursor = (slot + 1) % kClusterStatsSlots;
		}
	}
}

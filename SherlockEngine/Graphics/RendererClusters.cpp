#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "RHI/ResourceDesc.h"
#include "Graphics/ShaderConstants.h"
#include "Graphics/Frustum.h"
#include "Scene/Camera.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>
#include <numeric>

using namespace DirectX;   // 이 파일 안에서만

// 조명 컬링과 클러스터 (3단계, Renderer 의 멤버 함수를 파일만 나눠 구현함). 설계와 읽는 순서:
// docs/plan/deferred-clustered-ssao-record.html.
//
//   ComputeLightBounds  조명 하나의 경계 구 — 감쇠가 정확히 0 이 되는 범위(range, 바깥 원뿔)를 담는 구라 보수적
//   CullLights (3a)     메인 카메라 절두체로 지역광을 거르고, 그림자 후보를 "화면 기여도" 점수 순으로 줄 세움
//                       → BuildShadowViews 가 이 순서로 아틀라스 타일 16 장을 나눠 줌
//   BuildClusters (3b)  뷰마다: 화면 16×9 타일 × 깊이 24 슬라이스 = 3456 상자를 뷰 공간에 만들고, 지역광 경계 구와
//                       겹치는 상자에 조명 번호를 넣음 → (시작, 개수) 격자 + 번호 목록
//   UploadClusters      그 결과를 t30·t31 (프레임 셋, Dynamic) 에 올림. 셰이더 쪽은 Shaders/Clusters.hlsli
//
// BuildClusters·UploadClusters 는 이제 비교·대체 경로임 (--cluster-gpu=0, 또는 컴퓨트를 쓸 수 없을 때). 기본은 같은 결과를
// 컴퓨트 셰이더로 만드는 GPU 비닝 — Graphics/RendererClusterBin.cpp, docs/plan/gpu-cluster-binning-record.html. CullLights 는
// 그림자 점수 때문에 어느 경로든 CPU 에서 돎.
//
// 처음에 CPU 로 만든 이유: 이 엔진의 학습 순서상 컴퓨트 컬링 전에 "무엇을 어떻게 나누는가"를 눈으로 확인하려고. 조명마다 화면에
// 걸친 타일·슬라이스 범위만 검사함. 측정: Debug ManyLights256 메인 뷰 약 5 ms (2026-11-18), Release Intel Sponza 촛불 1만 개
// 씬 시점에 따라 3~17 ms (2026-11-21, docs/plan/gpu-cluster-binning-test.html).

namespace
{
	constexpr float kShadowHistoryBonus = 1.25f;   // 지난 프레임에 그림자를 받은 조명의 점수 배율 (경계에서 타일이 오락가락하지 않게)

	float Luminance(const XMFLOAT3& c)
	{
		return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
	}

	// 구와 축 정렬 상자 (뷰 공간) 의 겹침: 상자 안에서 구 중심에 가장 가까운 점까지의 거리² ≤ r².
	bool SphereIntersectsBox(const XMFLOAT3& c, float r, const XMFLOAT3& mn, const XMFLOAT3& mx)
	{
		const float dx = c.x < mn.x ? mn.x - c.x : (c.x > mx.x ? c.x - mx.x : 0.0f);
		const float dy = c.y < mn.y ? mn.y - c.y : (c.y > mx.y ? c.y - mx.y : 0.0f);
		const float dz = c.z < mn.z ? mn.z - c.z : (c.z > mx.z ? c.z - mx.z : 0.0f);
		return dx * dx + dy * dy + dz * dz <= r * r;
	}

	// 셰이더 ClusterSlice 와 같은 식. depth = (scale, bias, perspective, 0).
	int SliceOf(float z, const XMFLOAT4& depth)
	{
		const float f = depth.z > 0.5f ? std::log2((std::max)(z, 1.0e-4f)) : z;
		const int slice = static_cast<int>(std::floor(f * depth.x + depth.y));
		return std::clamp(slice, 0, static_cast<int>(kClusterGridZ) - 1);
	}
}

XMFLOAT4 Renderer::ComputeLightBounds(const GpuLight& light)
{
	if (light.type == static_cast<uint32_t>(LightType::Directional)) return XMFLOAT4(0.0f, 0.0f, 0.0f, -1.0f);   // 무한
	const float range = (std::max)(light.range, 0.0f);
	if (light.type == static_cast<uint32_t>(LightType::Spot))
	{
		// 스팟 = 원뿔(반각 θ, 높이 = range) 과 반경 range 구의 교집합. 이것을 담는 작은 구:
		//   θ ≤ 45°: 원뿔 밑면 원과 꼭짓점을 지나는 구. 중심 = 꼭짓점 + 방향 · h / (2 cosθ), 반지름 = h / (2 cosθ)
		//   θ > 45°: 밑면 원을 적도로 하는 구. 중심 = 꼭짓점 + 방향 · h cosθ, 반지름 = h sinθ
		//   θ ≥ 90°: 원뿔이 반구보다 넓음 → 포인트처럼 (꼭짓점, range)
		const float cosTheta = std::clamp(light.outerConeCos, -1.0f, 1.0f);
		if (cosTheta > 0.0f)
		{
			const float sinTheta = std::sqrt((std::max)(0.0f, 1.0f - cosTheta * cosTheta));
			const bool narrow = cosTheta >= 0.70710678f;   // θ ≤ 45°
			const float centerDistance = narrow ? range / (2.0f * cosTheta) : range * cosTheta;
			const float radius = narrow ? range / (2.0f * cosTheta) : range * sinTheta;
			return XMFLOAT4(light.position.x + light.direction.x * centerDistance, light.position.y + light.direction.y * centerDistance, light.position.z + light.direction.z * centerDistance, radius);
		}
	}
	return XMFLOAT4(light.position.x, light.position.y, light.position.z, range);
}

XMFLOAT4 Renderer::ClusterDepthParams(const Camera& camera)
{
	const float n = (std::max)(camera.GetNearZ(), 1.0e-4f);
	const float f = (std::max)(camera.GetFarZ(), n * 1.001f);
	const float slices = static_cast<float>(kClusterGridZ);
	if (camera.IsUsePerspectiveProjection())
	{
		// slice = log2(z) · S / log2(f/n) − S · log2(n) / log2(f/n). z = n 이면 0, z = f 면 S.
		const float logRatio = std::log2(f / n);
		return XMFLOAT4(slices / logRatio, -slices * std::log2(n) / logRatio, 1.0f, 0.0f);
	}
	// 직교: 원근 왜곡이 없으므로 균등 분할. slice = (z − n) · S / (f − n).
	return XMFLOAT4(slices / (f - n), -n * slices / (f - n), 0.0f, 0.0f);
}

void Renderer::CullLights(const Camera& camera)
{
	const uint32_t count = static_cast<uint32_t>(m_gpuLights.size());
	m_lightBounds.resize(count);
	m_lightVisible.assign(count, 1);
	m_shadowOrder.clear();

	const Frustum frustum = Frustum::FromViewProj(camera.GetViewMatrix() * camera.GetProjectionMatrix());
	const XMFLOAT3 eye = camera.GetPosition();
	struct Candidate
	{
		uint32_t index;
		float score;
	};
	std::vector<Candidate> candidates;
	candidates.reserve(count);

	for (uint32_t i = 0; i < count; ++i)
	{
		const GpuLight& light = m_gpuLights[i];
		m_lightBounds[i] = ComputeLightBounds(light);
		if (light.type == static_cast<uint32_t>(LightType::Directional)) continue;   // 방향광은 항상 보임, 그림자는 CSM 이 따로 받음

		// (1) 절두체 컬링: 경계 구가 절두체 밖이면 이 카메라에서는 그 조명의 빛이 닿은 픽셀이 하나도 없음.
		const XMFLOAT4& sphere = m_lightBounds[i];
		const bool visible = sphere.w > 0.0f && frustum.Intersects(sphere);
		m_lightVisible[i] = visible ? 1 : 0;
		if (!visible)
		{
			++m_stats.lightsCulled;
			// 컬링 고정 디버그 (lights 항목): camera 는 고정된 사본. 그리는 카메라의 클러스터는 m_lightBounds 를 직접 보고
			// 거르므로 (m_lightVisible 은 그림자 타일에만 씀), 그대로 두면 고정 시점 밖 조명도 자유 시점 화면을 계속 비춤.
			// w = 0 이면 CPU BuildClusters·GPU 비닝 둘 다 건너뜀 → 컬링된 조명의 빛이 꺼져 보임. 경계 배열이 두 뷰 공용이라
			// 미리보기 뷰에도 걸리고, 클러스터드를 끈 ShadeAllLights 경로(t15 전부)에는 효과 없음. 고정이 아니면 화면 밖
			// 조명은 원래 클러스터에 걸리지 않으므로 손대지 않음.
			if (m_cullFrozen && m_cullFreezeParts.lights)
			{
				m_lightBounds[i].w = 0.0f;
			}
			continue;
		}
		++m_stats.lightsVisible;

		// (2) 그림자 점수 = 화면에서 차지하는 정도 × 밝기. (r / D)² 은 경계 구가 화면에서 차지하는 입체각에 비례하는 근사
		//     (카메라가 구 안이면 1). 지난 프레임에 그림자를 받았으면 가산점 — 점수가 비슷한 두 조명이 프레임마다 자리를
		//     바꿔 그림자가 깜빡이는 것을 막음 (히스테리시스). 기록은 씬 조명 번호로 (정렬 뒤 번호는 프레임마다 같지만, 씬
		//     편집으로 방향광이 늘면 밀림).
		if (light.castShadows == 0) continue;
		const float dx = sphere.x - eye.x, dy = sphere.y - eye.y, dz = sphere.z - eye.z;
		const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		const float ratio = sphere.w / (std::max)(distance, sphere.w);
		float score = ratio * ratio * (std::max)(light.intensity, 0.0f) * Luminance(light.color);
		const uint32_t sceneIndex = i < m_lightSceneIndex.size() ? m_lightSceneIndex[i] : i;
		if (m_shadowHistory.count(sceneIndex) != 0)
		{
			score *= kShadowHistoryBonus;
		}
		candidates.push_back({ i, score });
	}

	// 점수 높은 순. 같으면 조명 번호 순 (결정적 — 스크린샷 비교가 실행마다 같도록).
	std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
	{
		if (a.score != b.score) return a.score > b.score;
		return a.index < b.index;
	});
	for (const Candidate& c : candidates)
	{
		m_shadowOrder.push_back(c.index);
	}
}

bool Renderer::IsLightCulledByFreeze(uint32_t sceneLightIndex) const
{
	if (!m_cullFrozen || !m_cullFreezeParts.lights) return false;
	// m_lightVisible 은 정렬 뒤(방향광이 앞) 번호 — 씬 번호로 거꾸로 찾음. 조명 수가 적어 선형 탐색.
	const size_t count = (std::min)(m_lightSceneIndex.size(), m_lightVisible.size());
	for (size_t i = 0; i < count; ++i)
	{
		if (m_lightSceneIndex[i] == sceneLightIndex) return m_lightVisible[i] == 0;
	}
	return false;
}

void Renderer::BuildClusters(const Camera& camera, uint32_t width, uint32_t height, uint32_t viewIndex)
{
	std::vector<uint32_t>& grid = m_clusterGridScratch[viewIndex];
	std::vector<uint32_t>& indices = m_clusterIndexScratch[viewIndex];
	grid.assign(kClusterCount * 2, 0);
	indices.clear();
	m_clusterBuilt[viewIndex] = true;
	if (!m_settings.clustered || width == 0 || height == 0) return;   // 빈 격자를 올림 (셰이더는 clusterParams.x 로 경로를 고름)

	const XMMATRIX view = camera.GetViewMatrix();
	XMFLOAT4X4 proj;
	XMStoreFloat4x4(&proj, camera.GetProjectionMatrix());
	const bool perspective = camera.IsUsePerspectiveProjection();
	const XMFLOAT4 depthParams = ClusterDepthParams(camera);
	const float nearZ = camera.GetNearZ();
	const float farZ = camera.GetFarZ();

	// ---- (1) 슬라이스 경계 z_k. 셰이더 ClusterSlice 의 역함수: 원근 z_k = n · (f/n)^(k/S), 직교 z_k = n + (f − n) · k/S ----
	float sliceZ[kClusterGridZ + 1];
	for (uint32_t k = 0; k <= kClusterGridZ; ++k)
	{
		const float t = static_cast<float>(k) / static_cast<float>(kClusterGridZ);
		sliceZ[k] = perspective ? nearZ * std::pow(farZ / nearZ, t) : nearZ + (farZ - nearZ) * t;
	}

	// ---- (2) 상자 3456 개 (뷰 공간 AABB). 타일의 NDC 사각형을 슬라이스의 앞·뒤 z 에서 뷰 공간으로 되돌린 꼭짓점 8 개를 감쌈 ----
	// LH 투영의 x: 원근 x_ndc = (P00 · x + P20 · z) / z → x = (x_ndc − P20) · z / P00, 직교 x_ndc = P00 · x + P30 → x = (x_ndc − P30) / P00.
	// y 도 같은 꼴 (P11, P21 / P31). 타일 y 는 위→아래 (SV_Position 방향) 라 NDC y 는 1 → −1.
	struct Box
	{
		XMFLOAT3 mn, mx;
	};
	static thread_local std::vector<Box> boxes;
	boxes.resize(kClusterCount);
	const auto toViewX = [&](float ndc, float z)
	{
		return perspective ? (ndc - proj._31) * z / proj._11 : (ndc - proj._41) / proj._11;
	};
	const auto toViewY = [&](float ndc, float z)
	{
		return perspective ? (ndc - proj._32) * z / proj._22 : (ndc - proj._42) / proj._22;
	};
	for (uint32_t k = 0; k < kClusterGridZ; ++k)
	{
		const float z0 = sliceZ[k], z1 = sliceZ[k + 1];
		for (uint32_t y = 0; y < kClusterGridY; ++y)
		{
			const float ny0 = 1.0f - 2.0f * static_cast<float>(y) / kClusterGridY;       // 타일 위
			const float ny1 = 1.0f - 2.0f * static_cast<float>(y + 1) / kClusterGridY;   // 타일 아래
			for (uint32_t x = 0; x < kClusterGridX; ++x)
			{
				const float nx0 = -1.0f + 2.0f * static_cast<float>(x) / kClusterGridX;
				const float nx1 = -1.0f + 2.0f * static_cast<float>(x + 1) / kClusterGridX;
				const float xs[4] = { toViewX(nx0, z0), toViewX(nx1, z0), toViewX(nx0, z1), toViewX(nx1, z1) };
				const float ys[4] = { toViewY(ny0, z0), toViewY(ny1, z0), toViewY(ny0, z1), toViewY(ny1, z1) };
				Box& box = boxes[x + kClusterGridX * (y + kClusterGridY * k)];
				box.mn = XMFLOAT3(*std::min_element(xs, xs + 4), *std::min_element(ys, ys + 4), z0);
				box.mx = XMFLOAT3(*std::max_element(xs, xs + 4), *std::max_element(ys, ys + 4), z1);
			}
		}
	}

	// ---- (3) 지역광마다: 뷰 공간 경계 구 → 걸치는 슬라이스·타일 범위 → 그 범위의 상자와 정밀 검사 → (상자, 조명) 쌍 ----
	// 조명 번호 오름차순으로 돌기 때문에 쌍도 조명 순서로 쌓임 → 상자 안 목록이 오름차순 (전체 순회와 같은 덧셈 순서).
	static thread_local std::vector<uint32_t> pairCluster;
	static thread_local std::vector<uint32_t> pairLight;
	pairCluster.clear();
	pairLight.clear();
	const uint32_t lightCount = static_cast<uint32_t>(m_gpuLights.size());
	for (uint32_t i = 0; i < lightCount; ++i)
	{
		const XMFLOAT4& bounds = m_lightBounds[i];
		if (bounds.w <= 0.0f) continue;   // 방향광(w < 0)은 셰이더가 따로 다 돎. 범위 0 은 빛이 없음
		XMFLOAT3 c;
		XMStoreFloat3(&c, XMVector3TransformCoord(XMVectorSet(bounds.x, bounds.y, bounds.z, 1.0f), view));
		const float r = bounds.w;
		if (c.z + r < nearZ || c.z - r > farZ) continue;   // 깊이 범위 밖

		const int k0 = SliceOf((std::max)(c.z - r, nearZ), depthParams);
		const int k1 = SliceOf((std::min)(c.z + r, farZ), depthParams);

		// 타일 범위: 구를 감싼 뷰 공간 상자의 꼭짓점을 투영한 NDC 의 최소·최대. 원근에서 구가 near 평면에 걸치면(꼭짓점의
		// z ≤ 0 이 생겨 투영이 뒤집힘) 화면 전체로 둠 — 보수적.
		int x0 = 0, x1 = kClusterGridX - 1, y0 = 0, y1 = kClusterGridY - 1;
		const bool crossesNear = perspective && (c.z - r <= nearZ);
		if (!crossesNear)
		{
			float minX = 1.0e30f, maxX = -1.0e30f, minY = 1.0e30f, maxY = -1.0e30f;
			for (int corner = 0; corner < 8; ++corner)
			{
				const float px = c.x + ((corner & 1) ? r : -r);
				const float py = c.y + ((corner & 2) ? r : -r);
				const float pz = c.z + ((corner & 4) ? r : -r);
				const float ndcX = perspective ? (proj._11 * px + proj._31 * pz) / pz : proj._11 * px + proj._41;
				const float ndcY = perspective ? (proj._22 * py + proj._32 * pz) / pz : proj._22 * py + proj._42;
				minX = (std::min)(minX, ndcX);
				maxX = (std::max)(maxX, ndcX);
				minY = (std::min)(minY, ndcY);
				maxY = (std::max)(maxY, ndcY);
			}
			if (maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f) continue;   // 화면 밖
			x0 = std::clamp(static_cast<int>(std::floor((minX + 1.0f) * 0.5f * kClusterGridX)), 0, static_cast<int>(kClusterGridX) - 1);
			x1 = std::clamp(static_cast<int>(std::floor((maxX + 1.0f) * 0.5f * kClusterGridX)), 0, static_cast<int>(kClusterGridX) - 1);
			y0 = std::clamp(static_cast<int>(std::floor((1.0f - maxY) * 0.5f * kClusterGridY)), 0, static_cast<int>(kClusterGridY) - 1);
			y1 = std::clamp(static_cast<int>(std::floor((1.0f - minY) * 0.5f * kClusterGridY)), 0, static_cast<int>(kClusterGridY) - 1);
		}

		for (int k = k0; k <= k1; ++k)
		{
			for (int y = y0; y <= y1; ++y)
			{
				for (int x = x0; x <= x1; ++x)
				{
					const uint32_t cluster = static_cast<uint32_t>(x) + kClusterGridX * (static_cast<uint32_t>(y) + kClusterGridY * static_cast<uint32_t>(k));
					const Box& box = boxes[cluster];
					if (!SphereIntersectsBox(c, r, box.mn, box.mx)) continue;
					pairCluster.push_back(cluster);
					pairLight.push_back(i);
				}
			}
		}
	}

	// ---- (4) 상자마다 개수 → 누적합으로 시작 위치 → 쌍을 순서대로 채움. 상한을 넘는 쌍은 버리고 통계에 남김 ----
	const uint32_t pairCount = static_cast<uint32_t>(pairCluster.size());
	for (uint32_t p = 0; p < pairCount; ++p)
	{
		++grid[pairCluster[p] * 2 + 1];
	}
	uint32_t offset = 0;
	uint32_t maxLights = 0;
	for (uint32_t cl = 0; cl < kClusterCount; ++cl)
	{
		uint32_t& cnt = grid[cl * 2 + 1];
		maxLights = (std::max)(maxLights, cnt);
		const uint32_t room = kMaxClusterLightRefs - offset;
		if (cnt > room)
		{
			if (viewIndex == 0)
			{
				m_stats.clusterOverflow += cnt - room;
			}
			cnt = room;
		}
		grid[cl * 2 + 0] = offset;
		offset += cnt;
	}
	indices.resize(offset);
	static thread_local std::vector<uint32_t> cursor;
	cursor.assign(kClusterCount, 0);
	for (uint32_t p = 0; p < pairCount; ++p)
	{
		const uint32_t cl = pairCluster[p];
		if (cursor[cl] >= grid[cl * 2 + 1]) continue;   // 상한으로 잘린 상자
		indices[grid[cl * 2 + 0] + cursor[cl]++] = pairLight[p];
	}
	if (viewIndex == 0)
	{
		m_stats.clusterLightRefs = offset;
		m_stats.clusterMaxLights = maxLights;
	}
}

void Renderer::EnsureClusterCapacity()
{
	uint32_t need = 1;
	for (uint32_t v = 0; v < kClusterViews; ++v)
	{
		if (m_clusterBuilt[v])
		{
			need = (std::max)(need, static_cast<uint32_t>(m_clusterIndexScratch[v].size()));
		}
	}
	if (need <= m_clusterIndexCapacity) return;

	// 두 배씩 (상한 kMaxClusterLightRefs — BuildClusters 가 이미 그 안으로 잘랐음). 프레임 셋(t31)이 옛 버퍼를 가리키므로
	// 바인딩 기록을 비워 다음 UpdateFrameSet 이 셋을 다시 만들게 함 (UploadLights 의 조명 버퍼 확장과 같은 방식).
	uint32_t capacity = (std::max)(m_clusterIndexCapacity, kInitialClusterLightRefs);
	while (capacity < need)
	{
		capacity *= 2;
	}
	capacity = (std::min)(capacity, kMaxClusterLightRefs);
	const BufferHandle grown = m_device->CreateBuffer(BufferDesc{ .size = capacity * static_cast<uint32_t>(sizeof(uint32_t)), .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_ShaderResource, .stride = sizeof(uint32_t), .debugName = "SB_ClusterLights" });
	if (!grown.IsValid())
	{
		Log::Error("클러스터 조명 목록 버퍼 확장 실패 (%u개). 넘치는 목록은 잘림.", capacity);
		return;
	}
	m_device->DestroyBuffer(m_clusterIndexBuffer);
	m_clusterIndexBuffer = grown;
	m_clusterIndexCapacity = capacity;
	m_boundEnvironment = EnvironmentBinding{};
	Log::Info("클러스터 조명 목록 버퍼 확장: %u개 (%u바이트)", capacity, capacity * static_cast<uint32_t>(sizeof(uint32_t)));
}

void Renderer::UploadClusters(uint32_t viewIndex)
{
	// D3D12 의 Dynamic 버퍼는 프레임마다 써야 바인딩됨 — 클러스터드가 꺼져 있어도 빈 격자를 올림.
	const std::vector<uint32_t>& grid = m_clusterGridScratch[viewIndex];
	const std::vector<uint32_t>& indices = m_clusterIndexScratch[viewIndex];
	static const std::vector<uint32_t> kEmptyGrid(kClusterCount * 2, 0);
	const bool built = m_clusterBuilt[viewIndex] && grid.size() == kClusterCount * 2;
	m_device->UpdateBuffer(m_clusterGridBuffer, built ? grid.data() : kEmptyGrid.data(), kClusterCount * 2 * static_cast<uint32_t>(sizeof(uint32_t)));
	static const uint32_t kNoIndex = 0;
	const uint32_t count = built ? (std::min)(static_cast<uint32_t>(indices.size()), m_clusterIndexCapacity) : 0;
	m_device->UpdateBuffer(m_clusterIndexBuffer, count > 0 ? indices.data() : &kNoIndex, (std::max)(count, 1u) * static_cast<uint32_t>(sizeof(uint32_t)));
}

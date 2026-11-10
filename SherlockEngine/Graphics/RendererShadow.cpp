#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/ResourceDesc.h"
#include "Graphics/ShaderConstants.h"
#include "Graphics/Frustum.h"
#include "Graphics/Mesh.h"
#include "Scene/Camera.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

// 그림자 (Renderer 의 멤버 함수를 파일만 나눠 구현함): 아틀라스 리소스, 조명 → 그림자 뷰 계산(방향광 단일 뷰·
// CSM·스팟·포인트 6면), b6 업로드, 아틀라스 패스.

namespace
{
	// ---- 섀도 아틀라스 배치 ----
	// 메인 아틀라스 (2048², 지역광 전용): 512² 타일 16개, 4×4. 타일 번호는 왼→오, 위→아래.
	//   ┌────┬────┬────┬────┐
	//   │ 0  │ 1  │ 2  │ 3  │
	//   ├────┼────┼────┼────┤   스팟 = 1타일, 포인트 = 연속 6타일 (행을 넘어가도 됨 — 뷰마다 사각형을 따로 가짐)
	//   │ 4  │ 5  │ 6  │ 7  │
	//   ├────┼────┼────┼────┤
	//   │ 8  │ 9  │ 10 │ 11 │
	//   ├────┼────┼────┼────┤
	//   │ 12 │ 13 │ 14 │ 15 │
	//   └────┴────┴────┴────┘
	// 방향광은 모두 CSM 전용 아틀라스(CsmRect, CsmSingleRect)에 그림.
	struct AtlasRect { uint32_t x, y, size; };

	// 방향광 단일 뷰(캐스케이드 1)는 CSM 아틀라스 전체를 씀.
	AtlasRect CsmSingleRect()
	{
		return { 0, 0, Renderer::kCsmAtlasWidth };
	}

	// CSM 전용 아틀라스 (2048×2048)의 캐스케이드 칸. 크기는 Renderer::kCsmCascadeSize. 배치를 바꾸면 헤더의
	// 그림도 같이 고칠 것.
	//   C0 = (0, 0), C1 = (1024, 0), C2 = (0, 1024), C3 = (1024, 1024) — 모두 1024
	AtlasRect CsmRect(uint32_t index)
	{
		index = (std::min)(index, 3u);
		static constexpr uint32_t kX[4] = { 0, 1024, 0, 1024 };
		static constexpr uint32_t kY[4] = { 0, 0, 1024, 1024 };
		return { kX[index], kY[index], Renderer::kCsmCascadeSize[index] };
	}

	// 지역광 타일 0..15. 메인 아틀라스의 4×4 격자 (왼→오, 위→아래).
	AtlasRect LocalTileRect(uint32_t tile)
	{
		constexpr uint32_t kColumns = Renderer::kShadowAtlasSize / Renderer::kShadowTileSize;
		tile = (std::min)(tile, Renderer::kShadowLocalTileCount - 1);
		return { (tile % kColumns) * Renderer::kShadowTileSize, (tile / kColumns) * Renderer::kShadowTileSize, Renderer::kShadowTileSize };
	}
}

bool Renderer::CreateShadowResources()
{
	// 깊이 전용 텍스처지만 셰이더에서도 읽음 → Device 가 TYPELESS 리소스 + DSV(D32) + SRV(R32) 로 만듦.
	const TextureDesc desc{
		.width = kShadowAtlasSize,
		.height = kShadowAtlasSize,
		.format = Format::D32_FLOAT,
		.mipLevels = 1,
		.bindFlags = TextureBind_DepthStencil | TextureBind_ShaderResource,
		.debugName = "ShadowMap",
	};
	m_shadowMap = m_device->CreateTexture(desc);

	// 방향광 CSM 전용 아틀라스. 캐스케이드마다 해상도가 달라서 Texture2DArray 대신 한 장에 칸을 나눠 씀
	// (배치는 CsmRect).
	const TextureDesc csmDesc{
		.width = kCsmAtlasWidth,
		.height = kCsmAtlasHeight,
		.format = Format::D32_FLOAT,
		.mipLevels = 1,
		.bindFlags = TextureBind_DepthStencil | TextureBind_ShaderResource,
		.debugName = "CsmShadowMap",
	};
	m_csmMap = m_device->CreateTexture(csmDesc);

	// 비교 샘플러: SampleCmp 가 "비교값(받는 쪽 깊이) ≤ 저장된 깊이" 면 1(빛 받음), 아니면 0 을 돌려주고
	// 이웃과 보간함(하드웨어 PCF). Border 1 은 아틀라스 전체 밖의 안전장치임. 각 뷰의 "뷰 밖 = 빛을 받음"은
	// PS 의 SampleShadowView 가 직접 판정하고, PCF 가 옆 타일을 읽지 않도록 타일 안쪽으로 clamp 함 (타일
	// 경계는 Border 가 아니라 이 clamp 가 막음).
	const SamplerDesc sampler{
		.filter = SamplerFilter::Comparison,
		.addressU = SamplerAddress::Border,
		.addressV = SamplerAddress::Border,
		.addressW = SamplerAddress::Border,
		.maxAnisotropy = 1,
		.compareFunc = CompareFunc::LessEqual,
		.borderColor = { 1.0f, 1.0f, 1.0f, 1.0f },
		.debugName = "Sampler_ShadowCompare",
	};
	m_shadowSampler = m_device->CreateSampler(sampler);

	if (!m_shadowMap.IsValid() || !m_csmMap.IsValid() || !m_shadowSampler.IsValid())
	{
		Log::Error("Renderer : 그림자 맵 리소스 생성 실패.");
		return false;
	}
	Log::Info("섀도 아틀라스: %ux%u D32_FLOAT (지역광 타일 %u × %u²), PCF 5x5", kShadowAtlasSize, kShadowAtlasSize,
		kShadowLocalTileCount, kShadowTileSize);
	Log::Info("CSM 아틀라스: %ux%u D32_FLOAT (캐스케이드 %u / %u / %u / %u)", kCsmAtlasWidth, kCsmAtlasHeight,
		kCsmCascadeSize[0], kCsmCascadeSize[1], kCsmCascadeSize[2], kCsmCascadeSize[3]);
	return true;
}

XMMATRIX Renderer::ComputeLightViewProj(const GpuLight& light) const
{
	// 방향광은 위치가 없음. 씬 중심에서 빛의 반대 방향으로 distance 만큼 물러난 곳에 눈을 두고,
	// 직교 투영으로 orthoSize × orthoSize 영역을 봄. 씬 경계를 알게 되면 그 AABB 에 맞추는 것이 다음 단계임.
	XMVECTOR direction = XMLoadFloat3(&light.direction);
	if (XMVectorGetX(XMVector3LengthSq(direction)) < 0.000001f)
	{
		direction = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	}
	direction = XMVector3Normalize(direction);

	const XMVECTOR center = XMVectorSet(0.0f, 2.0f, 0.0f, 1.0f);
	const XMVECTOR eye = XMVectorSubtract(center, XMVectorScale(direction, m_shadowSettings.distance));
	// 빛이 거의 수직이면 up = +Y 가 시선과 평행해져 LookAt 이 깨짐. 그때는 +Z 를 up 으로 씀.
	const XMVECTOR up = (fabsf(XMVectorGetY(direction)) > 0.99f) ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	const XMMATRIX view = XMMatrixLookAtLH(eye, center, up);
	const float size = m_shadowSettings.orthoSize;
	const XMMATRIX proj = XMMatrixOrthographicLH(size, size, 0.1f, m_shadowSettings.distance * 2.0f + size);
	return view * proj;
}

XMMATRIX Renderer::ComputeSpotViewProj(const GpuLight& light, float& outTanHalfFov) const
{
	XMVECTOR direction = XMLoadFloat3(&light.direction);
	if (XMVectorGetX(XMVector3LengthSq(direction)) < 0.000001f)
	{
		direction = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	}
	direction = XMVector3Normalize(direction);

	const XMVECTOR eye = XMVectorSet(light.position.x, light.position.y, light.position.z, 1.0f);
	// ComputeLightViewProj 와 같은 규칙: 빛이 거의 수직이면 up = +Z
	const XMVECTOR up = (fabsf(XMVectorGetY(direction)) > 0.99f) ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	const XMMATRIX view = XMMatrixLookToLH(eye, direction, up);

	// 시야각 = 바깥 원뿔 전체 + 2° 여유. [5°, 120°] 로 clamp — 바깥 원뿔이 60° 를 넘으면 가장자리는 그림자가 없음.
	const float outerCos = std::clamp(light.outerConeCos, -1.0f, 1.0f);
	const float fov = std::clamp(2.0f * acosf(outerCos) + XMConvertToRadians(2.0f), XMConvertToRadians(5.0f), XMConvertToRadians(120.0f));
	const float nearZ = (light.range < 0.2f) ? light.range * 0.5f : 0.1f;
	const float farZ = (std::max)(light.range, nearZ + 0.01f);   // 괄호: Windows.h 의 max 매크로 회피
	outTanHalfFov = tanf(fov * 0.5f);
	return view * XMMatrixPerspectiveFovLH(fov, 1.0f, nearZ, farZ);
}

XMMATRIX Renderer::ComputePointFaceViewProj(const GpuLight& light, uint32_t face) const
{
	// 면 순서는 D3D 큐브맵과 같음 (+X, −X, +Y, −Y, +Z, −Z). PS 의 PointFace 가 같은 번호를 씀.
	static const XMFLOAT3 kLook[6] = { {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1} };
	static const XMFLOAT3 kUp[6] = { {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0} };
	face = (std::min)(face, 5u);

	const XMVECTOR eye = XMVectorSet(light.position.x, light.position.y, light.position.z, 1.0f);
	const XMMATRIX view = XMMatrixLookToLH(eye, XMLoadFloat3(&kLook[face]), XMLoadFloat3(&kUp[face]));

	// 시야각을 90° 보다 조금 넓힘: PCF 가 타일 가장자리 kShadowPcfBorder 텍셀 안쪽으로 clamp 되므로, 그
	// 안쪽이 정확히 90° 를 덮게 함.
	const float tanHalf = static_cast<float>(kShadowTileSize) / static_cast<float>(kShadowTileSize - 2 * kShadowPcfBorder);
	const float fov = 2.0f * atanf(tanHalf);
	return view * XMMatrixPerspectiveFovLH(fov, 1.0f, 0.05f, (std::max)(light.range, 0.06f));
}

uint32_t Renderer::AddShadowView(const XMMATRIX& viewProj, uint32_t x, uint32_t y, uint32_t size,
	const XMFLOAT4& params, uint32_t lightIndex, uint32_t atlas)
{
	static bool warnedFull = false;
	if (m_shadowViewCount >= MAX_SHADOW_VIEWS)
	{
		if (!warnedFull)
		{
			warnedFull = true;
			Log::Error("그림자 뷰가 가득 참 (최대 %u개). 이 뷰는 만들지 않음.", MAX_SHADOW_VIEWS);
		}
		return UINT32_MAX;
	}
	const uint32_t index = m_shadowViewCount++;
	ShadowView& view = m_shadowViews[index];
	XMStoreFloat4x4(&view.viewProj, viewProj);   // 행우선 그대로. 올릴 때 전치함
	view.x = x;
	view.y = y;
	view.size = size;
	view.atlas = atlas;
	view.params = params;
	view.cullSphere = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	view.lightIndex = lightIndex;
	return index;
}

uint32_t Renderer::AddCascadeViews(const Camera& camera, const GpuLight& light, uint32_t lightIndex)
{
	const uint32_t count = static_cast<uint32_t>(std::clamp(m_shadowSettings.cascadeCount, 2, 4));
	if (m_shadowViewCount + count > MAX_SHADOW_VIEWS) return UINT32_MAX;   // 캐스케이드는 번호가 연속이어야 하므로 통째로 거절

	// ---- 분할 (practical split): s[i] = λ · n · (f/n)^(i/N) + (1 − λ) · (n + (f − n) · i/N), s[0] = n, s[N] = f ----
	const float nearZ = camera.GetNearZ();
	const float farZ = (std::max)((std::min)(camera.GetFarZ(), m_shadowSettings.cascadeDistance), nearZ + 0.01f);
	const float lambda = std::clamp(m_shadowSettings.cascadeLambda, 0.0f, 1.0f);
	float split[5] = {};
	for (uint32_t i = 0; i <= count; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(count);
		split[i] = lambda * nearZ * powf(farZ / nearZ, t) + (1.0f - lambda) * (nearZ + (farZ - nearZ) * t);
	}

	// ---- 카메라와 광원 방향 ----
	const XMFLOAT3 cameraPosition = camera.GetPosition();
	const XMVECTOR eyePosition = XMLoadFloat3(&cameraPosition);
	const XMVECTOR forward = camera.GetForward();
	const XMVECTOR right = camera.GetRight();
	const XMVECTOR cameraUp = camera.GetUp();
	const float tanHalfFovY = tanf(camera.GetFovY() * 0.5f);
	const float aspect = camera.GetAspect();

	XMVECTOR direction = XMLoadFloat3(&light.direction);
	if (XMVectorGetX(XMVector3LengthSq(direction)) < 0.000001f) direction = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	direction = XMVector3Normalize(direction);
	// ComputeLightViewProj 와 같은 up 규칙
	const XMVECTOR lightUp = (fabsf(XMVectorGetY(direction)) > 0.99f) ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	const float back = m_shadowSettings.distance;   // 조각 뒤(광원 쪽)로 캐스터를 잡는 거리

	uint32_t first = UINT32_MAX;
	for (uint32_t c = 0; c < count; ++c)
	{
		// 1) 조각의 꼭짓점 8개: 앞면(split[c])과 뒷면(split[c+1])의 네 모서리
		XMVECTOR corners[8];
		for (uint32_t k = 0; k < 2; ++k)
		{
			const float d = split[c + k];
			const float h = d * tanHalfFovY;
			const float w = h * aspect;
			const XMVECTOR center = XMVectorAdd(eyePosition, XMVectorScale(forward, d));
			for (uint32_t m = 0; m < 4; ++m)
			{
				const float sx = (m & 1) ? 1.0f : -1.0f;
				const float sy = (m & 2) ? 1.0f : -1.0f;
				corners[k * 4 + m] = XMVectorAdd(center, XMVectorAdd(XMVectorScale(right, sx * w), XMVectorScale(cameraUp, sy * h)));
			}
		}

		for (uint32_t k = 0; k < 8; ++k) XMStoreFloat3(&m_cascadeCorners[c][k], corners[k]);   // 에디터 오버레이용

		// 2) 경계 구: 중심 = 꼭짓점 평균, 반지름 = 가장 먼 꼭짓점까지. 반지름을 1/16 단위로 올림 — 카메라가
		//    돌아도 투영 크기가 일정해 텍셀 스냅이 유효함.
		XMVECTOR sphereCenter = XMVectorZero();
		for (const XMVECTOR& corner : corners) sphereCenter = XMVectorAdd(sphereCenter, corner);
		sphereCenter = XMVectorScale(sphereCenter, 1.0f / 8.0f);
		float radius = 0.0f;
		for (const XMVECTOR& corner : corners) radius = (std::max)(radius, XMVectorGetX(XMVector3Length(XMVectorSubtract(corner, sphereCenter))));
		radius = ceilf(radius * 16.0f) / 16.0f;

		// 3) 광원 뷰: 눈 = 구 중심에서 빛의 반대로 (r + back). 직교 상자 한 변 2r, 깊이 0 .. 2r + back.
		const XMVECTOR lightEye = XMVectorSubtract(sphereCenter, XMVectorScale(direction, radius + back));
		const XMMATRIX view = XMMatrixLookToLH(lightEye, direction, lightUp);
		XMMATRIX proj = XMMatrixOrthographicLH(2.0f * radius, 2.0f * radius, 0.0f, 2.0f * radius + back);

		// 4) 텍셀 스냅: 월드 원점이 광원 클립에서 놓이는 위치를 텍셀 격자에 맞춤. 카메라가 움직여도 그림자
		//    텍셀이 월드에 붙어 있어 가장자리가 반짝이지 않음. 격자는 이 캐스케이드의 해상도(캐스케이드마다
		//    다름) 기준이어야 함.
		const AtlasRect rect = CsmRect(c);
		const float tile = static_cast<float>(rect.size);
		XMVECTOR origin = XMVector4Transform(XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), view * proj);
		origin = XMVectorScale(origin, tile * 0.5f);   // NDC [-1,1] → 텍셀 단위
		const XMVECTOR snapped = XMVectorRound(origin);
		const XMVECTOR offset = XMVectorScale(XMVectorSubtract(snapped, origin), 2.0f / tile);
		proj.r[3] = XMVectorAdd(proj.r[3], XMVectorSet(XMVectorGetX(offset), XMVectorGetY(offset), 0.0f, 0.0f));

		// 5) 사각형과 params. 바이어스는 단일 뷰와 같은 "월드 거리"가 되도록 깊이 범위 비로 환산함.
		const float singleRange = m_shadowSettings.distance * 2.0f + m_shadowSettings.orthoSize;
		const XMFLOAT4 params(m_shadowSettings.bias * singleRange / (2.0f * radius + back), 2.0f * radius / rect.size, 0.0f, 0.0f);
		const uint32_t index = AddShadowView(view * proj, rect.x, rect.y, rect.size, params, lightIndex, 1);   // 1 = CSM 아틀라스
		if (c == 0) first = index;
	}

	// PS 가 캐스케이드를 고를 때 쓰는 값. 카메라 기준으로 한 번만 만듦.
	m_cascadeCount = count;
	m_cascadeSplits = XMFLOAT4(split[1], count > 1 ? split[2] : 0.0f, count > 2 ? split[3] : 0.0f, count > 3 ? split[4] : 0.0f);
	m_cascadeOrigin = cameraPosition;
	XMStoreFloat3(&m_cascadeForward, forward);
	return first;
}

void Renderer::SetCascadeFreeze(bool freeze, const Camera& camera)
{
	m_cascadeFrozen = freeze;
	if (freeze) m_cascadeFreezeCamera = camera;
}

void Renderer::BuildShadowViews(const Camera& drawCamera, std::vector<GpuLight>& lights)
{
	// 고정 중이면 캐스케이드는 잡아 둔 사본 기준. 로컬 광원 등 나머지는 카메라를 쓰지 않음.
	const Camera& camera = m_cascadeFrozen ? m_cascadeFreezeCamera : drawCamera;
	m_shadowViewCount = 0;
	m_cascadeCount = 0;
	m_cascadeSplits = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	m_skippedShadowLights = 0;
	m_usedLocalTiles = 0;
	for (GpuLight& light : lights) light.shadowIndex = -1;
	if (!m_settings.shadows) return;

	bool directionalDone = false;
	for (uint32_t i = 0; i < static_cast<uint32_t>(lights.size()); ++i)
	{
		GpuLight& light = lights[i];
		if (light.castShadows == 0) continue;

		if (light.type == static_cast<uint32_t>(LightType::Directional))
		{
			// 방향광은 첫 번째 castShadows 방향광만. 두 번째부터는 자리를 주지 않음.
			if (directionalDone)
			{
				++m_skippedShadowLights;
				continue;
			}
			directionalDone = true;
			if (m_shadowSettings.cascadeEnabled && camera.IsUsePerspectiveProjection())
			{
				// CSM. 직교 카메라는 절두체 조각 계산이 맞지 않아 아래의 단일 뷰로 대신함.
				const uint32_t first = AddCascadeViews(camera, light, i);
				if (first != UINT32_MAX) light.shadowIndex = static_cast<int32_t>(first);
				else ++m_skippedShadowLights;
				continue;
			}
			// 단일 뷰도 CSM 전용 아틀라스(atlas 1)에 그림. 텍셀의 월드 크기는 그 뷰의 해상도 기준.
			const AtlasRect rect = CsmSingleRect();
			const XMFLOAT4 params(m_shadowSettings.bias, m_shadowSettings.orthoSize / rect.size, 0.0f, 0.0f);
			const uint32_t view = AddShadowView(ComputeLightViewProj(light), rect.x, rect.y, rect.size, params, i, 1);
			if (view != UINT32_MAX) light.shadowIndex = static_cast<int32_t>(view);
		}
		else if (light.type == static_cast<uint32_t>(LightType::Spot))
		{
			if (m_usedLocalTiles >= kShadowLocalTileCount)
			{
				++m_skippedShadowLights;
				continue;
			}
			float tanHalf = 1.0f;
			const XMMATRIX viewProj = ComputeSpotViewProj(light, tanHalf);
			const AtlasRect rect = LocalTileRect(m_usedLocalTiles++);
			// y = 거리 1 에서 텍셀 하나가 덮는 월드 길이 (2 · tan(fov/2) / 해상도). 셰이더가 거리를 곱함. z = 1 원근.
			const XMFLOAT4 params(0.0f, 2.0f * tanHalf / rect.size, 1.0f, 0.0f);
			const uint32_t view = AddShadowView(viewProj, rect.x, rect.y, rect.size, params, i);
			if (view != UINT32_MAX)
			{
				light.shadowIndex = static_cast<int32_t>(view);
				m_shadowViews[view].cullSphere = XMFLOAT4(light.position.x, light.position.y, light.position.z, light.range);
			}
		}
		else if (light.type == static_cast<uint32_t>(LightType::Point))
		{
			// 면 6개가 연속 타일이어야 함 (PS 가 shadowIndex + face 로 찾음). 6칸이 안 남으면 일부만 주지
			// 않고 통째로 건너뜀.
			if (m_usedLocalTiles + 6 > kShadowLocalTileCount || m_shadowViewCount + 6 > MAX_SHADOW_VIEWS)
			{
				++m_skippedShadowLights;
				continue;
			}
			const float tanHalf = static_cast<float>(kShadowTileSize) / static_cast<float>(kShadowTileSize - 2 * kShadowPcfBorder);
			const XMFLOAT4 params(0.0f, 2.0f * tanHalf / kShadowTileSize, 1.0f, 0.0f);
			uint32_t first = UINT32_MAX;
			for (uint32_t face = 0; face < 6; ++face)
			{
				const AtlasRect rect = LocalTileRect(m_usedLocalTiles++);
				const uint32_t view = AddShadowView(ComputePointFaceViewProj(light, face), rect.x, rect.y, rect.size, params, i);
				if (view == UINT32_MAX) break;   // 위에서 자리를 확인했으므로 일어나지 않음
				if (face == 0) first = view;
				m_shadowViews[view].cullSphere = XMFLOAT4(light.position.x, light.position.y, light.position.z, light.range);
			}
			if (first != UINT32_MAX) light.shadowIndex = static_cast<int32_t>(first);
		}
	}
}

void Renderer::UploadShadowConstants()
{
	// b6: 그림자 뷰 배열. 그림자가 꺼진 프레임(뷰 0개)에도 올림 — D3D12 는 Dynamic 상수버퍼가 프레임마다
	// 갱신되어야 경고하지 않음.
	ShadowConstants constants = {};
	for (uint32_t i = 0; i < m_shadowViewCount; ++i)
	{
		const ShadowView& view = m_shadowViews[i];
		ShadowViewData& data = constants.views[i];
		XMStoreFloat4x4(&data.viewProj, XMMatrixTranspose(XMLoadFloat4x4(&view.viewProj)));
		// 아틀라스마다 크기가 다름 (CSM 아틀라스는 가로·세로도 다름). UV 사각형은 그 뷰가 속한 아틀라스
		// 기준으로 환산함.
		const float width = static_cast<float>(view.atlas == 1 ? kCsmAtlasWidth : kShadowAtlasSize);
		const float height = static_cast<float>(view.atlas == 1 ? kCsmAtlasHeight : kShadowAtlasSize);
		data.atlasRect = XMFLOAT4(view.x / width, view.y / height, view.size / width, view.size / height);
		data.params = view.params;
		data.params.w = static_cast<float>(view.atlas);   // 셰이더가 t8 / t14 중 어느 쪽을 읽을지 고름
	}
	constants.atlasParams = XMFLOAT4(1.0f / kShadowAtlasSize, static_cast<float>(kShadowPcfBorder), m_shadowSettings.localNormalBias, 0.0f);
	constants.csmAtlasParams = XMFLOAT4(1.0f / kCsmAtlasWidth, 1.0f / kCsmAtlasHeight, 0.0f, 0.0f);
	// CSM 이 아니면 cascadeOrigin.w = 0 (PS 가 단일 뷰 경로를 탐)
	constants.cascadeSplits = m_cascadeSplits;
	constants.cascadeOrigin = XMFLOAT4(m_cascadeOrigin.x, m_cascadeOrigin.y, m_cascadeOrigin.z, static_cast<float>(m_cascadeCount));
	constants.cascadeForward = XMFLOAT4(m_cascadeForward.x, m_cascadeForward.y, m_cascadeForward.z, std::clamp(m_shadowSettings.cascadeBlend, 0.0f, 0.5f));   // w = 경계 블렌딩 비율
	m_device->UpdateBuffer(m_shadowsCB, &constants, sizeof(constants));

	// b5: 그림자 패스가 뷰마다 덮어쓸 자리. 여기서는 단위 행렬로 한 번 올려 둠 (그림자 패스가 없는 프레임의
	// 갱신 검사용).
	ShadowPassConstants pass = {};
	XMStoreFloat4x4(&pass.shadowViewProj, XMMatrixIdentity());
	m_device->UpdateBuffer(m_shadowPassCB, &pass, sizeof(pass));
}

void Renderer::RenderShadowPass(RHI::CommandList& cmd)
{
	m_stats.shadowViews = m_shadowViewCount;

	// 깊이 텍스처가 둘이라 패스도 둘: 0 = 메인 아틀라스(지역광), 1 = CSM 전용 아틀라스(방향광).
	RenderShadowAtlasPass(cmd, 0);
	RenderShadowAtlasPass(cmd, 1);
}

void Renderer::RenderShadowAtlasPass(RHI::CommandList& cmd, uint32_t atlas)
{
	const TextureHandle map = (atlas == 1) ? m_csmMap : m_shadowMap;

	// 이 아틀라스에 그릴 뷰가 없으면 패스를 열지 않음. 메인 패스가 t8 / t14 를 SRV 로 읽으므로 상태만
	// ShaderResource 로 맞춤 (이미 그 상태면 RHI 가 아무것도 하지 않음).
	bool hasViews = false;
	for (uint32_t v = 0; v < m_shadowViewCount; ++v) hasViews = hasViews || (m_shadowViews[v].atlas == atlas);
	if (!hasViews)
	{
		cmd.Barrier(map, ResourceState::ShaderResource);
		return;
	}

	// 그림자 맵: ShaderResource(또는 Common) → DepthWrite. D3D11 에서는 상태 기록만 하고, D3D12 에서는 실제
	// 배리어를 기록함. "지금 상태"는 RHI 가 텍스처마다 기억함.
	cmd.Barrier(map, ResourceState::DepthWrite);

	// RenderPassDesc 도 지정 초기화 — Vulkan 의 VkRenderingInfo 나 D3D12 의 BeginRenderPass 인자와 같은 모양이 됨.
	const RenderPassDesc pass{
		.colorCount = 0,
		.depth = { .texture = map, .load = LoadOp::Clear, .clearDepth = 1.0f },
		.debugName = (atlas == 1) ? "CsmShadowPass" : "ShadowPass",
	};
	cmd.BeginRenderPass(pass);   // 아틀라스 전체를 한 번 클리어. 뷰포트는 뷰마다 아래에서 바꿈

	PipelineHandle lastPipeline;
	const GpuMesh* lastMesh = nullptr;
	for (uint32_t v = 0; v < m_shadowViewCount; ++v)
	{
		const ShadowView& view = m_shadowViews[v];
		if (view.atlas != atlas) continue;

		// ① 이 뷰의 아틀라스 사각형으로 뷰포트(D3D12 는 시저도) 제한
		cmd.SetViewport(Viewport{ .x = static_cast<float>(view.x), .y = static_cast<float>(view.y),
			.width = static_cast<float>(view.size), .height = static_cast<float>(view.size) });
		// ② b5 에 이 뷰의 행렬 (전치). 드로우 직전에 D3D12 가 루트 CBV 를 다시 바인딩하므로 프레임 셋을 다시
		// 꽂을 필요 없음.
		ShadowPassConstants passConstants = {};
		XMStoreFloat4x4(&passConstants.shadowViewProj, XMMatrixTranspose(XMLoadFloat4x4(&view.viewProj)));
		m_device->UpdateBuffer(m_shadowPassCB, &passConstants, sizeof(passConstants));

		// ③ 이 뷰의 절두체. 원근 뷰(스팟·포인트 면, params.z = 1)는 평면 여섯 개를 다 봄. 직교 뷰(방향광·
		// 캐스케이드)는 near 평면을 빼서, 광원 쪽으로 더 물러나 있는 캐스터를 보수적으로 남김.
		const bool perspective = view.params.z == 1.0f;
		const Frustum frustum = Frustum::FromViewProj(XMLoadFloat4x4(&view.viewProj), !perspective);

		// ④ 드로우 루프
		for (const DrawItem& item : m_drawList)
		{
			if (!item.shadowPipeline.IsValid()) continue;   // unlit 재질 (BuildDrawList 가 빈 핸들로 둠)

			const XMFLOAT4& sphere = m_objects[item.object].sphere;
			// 지역광(cullSphere.w > 0): 광원 범위 구와 겹치지 않는 물체는 이 뷰에 그림자를 드리울 수 없음.
			// 방향광·캐스케이드는 w = 0 이라 이 검사는 하지 않음.
			if (view.cullSphere.w > 0.0f)
			{
				const float dx = sphere.x - view.cullSphere.x, dy = sphere.y - view.cullSphere.y, dz = sphere.z - view.cullSphere.z;
				const float reach = sphere.w + view.cullSphere.w;
				if (dx * dx + dy * dy + dz * dz > reach * reach)
				{
					++m_stats.shadowCulled;
					continue;
				}
			}
			// 뷰 절두체 밖: 포인트 광원의 반대쪽 면, 캐스케이드 상자 옆으로 벗어난 물체 등.
			if (!frustum.Intersects(sphere))
			{
				++m_stats.shadowCulled;
				continue;
			}

			if (item.shadowPipeline != lastPipeline)
			{
				cmd.SetPipelineState(item.shadowPipeline);
				lastPipeline = item.shadowPipeline;
				++m_stats.pipelineSwitches;
			}
			if (item.gpuMesh != lastMesh)
			{
				cmd.SetVertexBuffer(item.gpuMesh->vertexBuffer);
				cmd.SetIndexBuffer(item.gpuMesh->indexBuffer, item.gpuMesh->indexFormat);
				lastMesh = item.gpuMesh;
				++m_stats.meshSwitches;
			}
			UploadObjectConstants(item.object);
			cmd.DrawIndexed(item.indexCount, item.indexStart, 0);
			++m_stats.shadowDraws;
		}
	}

	cmd.EndRenderPass();

	// DepthWrite → ShaderResource. 메인 패스가 t8 / t14 로 읽음.
	cmd.Barrier(map, ResourceState::ShaderResource);
}

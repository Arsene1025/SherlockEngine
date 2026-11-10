#pragma once
#include <cstdint>
#include <DirectXMath.h>

// 절두체 컬링. 뷰·투영 행렬에서 평면 여섯 개를 뽑고, 월드 경계 구가 그 안에 걸치는지 봄.
//
// 이 엔진은 행벡터 관례임 (clip = v · M). clip 의 각 성분은 v 와 M 의 "열"의 내적이므로, 절두체 조건
//   −w ≤ x ≤ w,  −w ≤ y ≤ w,  0 ≤ z ≤ w        (D3D 클립 공간. z 는 0..w)
// 을 열로 풀면 평면 계수가 바로 나옴 (Gribb–Hartmann):
//   왼쪽 col3 + col0,  오른쪽 col3 − col0,  아래 col3 + col1,  위 col3 − col1,  near col2,  far col3 − col2
// 평면은 "안쪽이 양수"이고 법선 길이로 나눠 두므로, 점과의 내적이 곧 부호 있는 거리임.
struct Frustum
{
	DirectX::XMFLOAT4 planes[6];   // xyz = 안쪽을 향한 단위 법선, w = d
	uint32_t count = 0;

	// skipNear: near 평면 검사를 뺌. 직교 그림자 뷰에서 광원 쪽으로 더 가까이 있는 물체를 보수적으로 남길 때 씀.
	static Frustum FromViewProj(DirectX::FXMMATRIX viewProj, bool skipNear = false)
	{
		using namespace DirectX;
		const XMMATRIX columns = XMMatrixTranspose(viewProj);   // r[i] = 원래 행렬의 i 번째 열
		const XMVECTOR candidates[6] = {
			XMVectorAdd(columns.r[3], columns.r[0]),
			XMVectorSubtract(columns.r[3], columns.r[0]),
			XMVectorAdd(columns.r[3], columns.r[1]),
			XMVectorSubtract(columns.r[3], columns.r[1]),
			columns.r[2],
			XMVectorSubtract(columns.r[3], columns.r[2]),
		};
		Frustum frustum;
		for (uint32_t i = 0; i < 6; ++i)
		{
			if (skipNear && i == 4) continue;
			const float length = XMVectorGetX(XMVector3Length(candidates[i]));
			if (length < 1e-12f) continue;   // 퇴화한 평면 (잘못된 행렬). 검사에서 빼면 보수적으로 남김
			XMStoreFloat4(&frustum.planes[frustum.count++], XMVectorScale(candidates[i], 1.0f / length));
		}
		return frustum;
	}

	// 구가 절두체와 겹치거나 안에 있으면 true. 평면별 검사라 모서리 근처에서는 밖에 있는 구를 남길 수 있음
	// (보수적 — 그려야 할 것을 버리는 일은 없음).
	bool Intersects(const DirectX::XMFLOAT4& sphere) const
	{
		for (uint32_t i = 0; i < count; ++i)
		{
			const DirectX::XMFLOAT4& p = planes[i];
			if (p.x * sphere.x + p.y * sphere.y + p.z * sphere.z + p.w < -sphere.w) return false;
		}
		return true;
	}
};

#pragma once
#include <cmath>
#include <cstdint>
#include <DirectXMath.h>

// 게임 UI(캔버스)의 작은 수학 타입. docs/plan/ui-canvas-plan.html 2.1 참고.
//
// 좌표계: 캔버스 단위(CanvasScaler 를 거친 논리 픽셀), 원점은 부모 사각형의 왼쪽 아래, y 는 위가 + (유니티와 같음).
// 화면 픽셀(y 아래가 +)로는 캔버스 행렬(UIDrawList::canvasMatrices)이 바꿈.

// 축 정렬 사각형 (min = 왼쪽 아래, max = 오른쪽 위)
struct UIRect
{
	DirectX::XMFLOAT2 min{ 0.0f, 0.0f };
	DirectX::XMFLOAT2 max{ 0.0f, 0.0f };

	float Width() const { return max.x - min.x; }
	float Height() const { return max.y - min.y; }
};

// 2D 아핀 변환: x' = a·x + c·y + tx, y' = b·x + d·y + ty. 요소의 로컬(부모 공간) → 캔버스.
struct UIAffine
{
	float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, tx = 0.0f, ty = 0.0f;

	DirectX::XMFLOAT2 Apply(const DirectX::XMFLOAT2& p) const
	{
		return DirectX::XMFLOAT2(a * p.x + c * p.y + tx, b * p.x + d * p.y + ty);
	}
	// 결과 = this ∘ inner (inner 를 먼저 적용)
	UIAffine Then(const UIAffine& inner) const
	{
		UIAffine r;
		r.a = a * inner.a + c * inner.b;
		r.b = b * inner.a + d * inner.b;
		r.c = a * inner.c + c * inner.d;
		r.d = b * inner.c + d * inner.d;
		r.tx = a * inner.tx + c * inner.ty + tx;
		r.ty = b * inner.tx + d * inner.ty + ty;
		return r;
	}
	bool Invert(UIAffine& out) const
	{
		const float det = a * d - b * c;
		if (std::fabs(det) < 1e-12f) return false;
		const float inv = 1.0f / det;
		out.a = d * inv;
		out.b = -b * inv;
		out.c = -c * inv;
		out.d = a * inv;
		out.tx = -(out.a * tx + out.c * ty);
		out.ty = -(out.b * tx + out.d * ty);
		return true;
	}
	bool IsIdentity() const { return a == 1.0f && b == 0.0f && c == 0.0f && d == 1.0f && tx == 0.0f && ty == 0.0f; }
};

// sRGB 값(0~1) → RGBA8 (r 이 가장 낮은 바이트 = R8G8B8A8_UNORM 정점 속성)
inline uint32_t PackUIColor(const DirectX::XMFLOAT4& c)
{
	auto to8 = [](float v) -> uint32_t
	{
		v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
		return static_cast<uint32_t>(v * 255.0f + 0.5f);
	};
	return to8(c.x) | (to8(c.y) << 8) | (to8(c.z) << 16) | (to8(c.w) << 24);
}

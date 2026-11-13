#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include <DirectXMath.h>

// 디버그 선 정점. color = R8G8B8A8_UNORM (r | g<<8 | b<<16 | a<<24). 월드 공간 위치.
struct DebugLineVertex { DirectX::XMFLOAT3 position; uint32_t color; };

// (std::min)/(std::max) 괄호는 windows.h 의 min/max 매크로를 피하려는 것.
inline uint32_t PackLineColor(float r, float g, float b)
{
	auto channel = [](float v) { return static_cast<uint32_t>((std::min)((std::max)(v, 0.0f), 1.0f) * 255.0f + 0.5f); };
	return channel(r) | (channel(g) << 8) | (channel(b) << 16) | (255u << 24);
}

// DebugDraw: 이번 프레임에 그릴 디버그 선을 모으는 CPU 쪽 수집기. GPU(RHI)를 모름 — Renderer 가 GetVertices() 를 읽어 동적
// 정점 버퍼에 올리고 그림. 본·Raycast·충돌 형상 같은 기능은 각자 이 객체에 선을 쌓기만 하면 되고 Renderer 를 알 필요가 없음.
//
// 수명: Engine 이 소유. 프레임 안 아무 때나 쌓고, Engine::Render 가 Renderer::Render 가 끝난 직후 Clear 함 (Render 가 일찍
// 끝나는 프레임에도 선이 쌓이지 않도록 Renderer 가 아니라 Engine 이 비움). 쌓인 선은 한 프레임만 살아 있음 — 지속 시간(duration)은
// 필요해질 때 추가.
//
// 깊이: 지금은 깊이 테스트 없이 항상 위에 그림 (본 표시에 맞음). 충돌 형상처럼 가려져야 하는 선이 생기면 선마다 깊이 모드를 갖는
// 두 번째 버킷과 PSO 를 더함.
class DebugDraw
{
public:
	// 정점 상한 (선 하나 = 정점 2개). Renderer 의 동적 정점 버퍼 크기도 이 값 (16바이트 × 65536 = 1MB). 넘치면 선을 버림.
	static constexpr uint32_t kMaxVertices = 65536;

	void Clear() { m_vertices.clear(); }
	const std::vector<DebugLineVertex>& GetVertices() const { return m_vertices; }
	uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_vertices.size()); }

	void Line(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, uint32_t color)
	{
		if (m_vertices.size() + 2 > kMaxVertices) return;
		m_vertices.push_back({ a, color });
		m_vertices.push_back({ b, color });
	}

	// 위치 p 에 축 방향 십자(x → y → z 순). halfSize = 한 방향 길이.
	void Cross(const DirectX::XMFLOAT3& p, float halfSize, uint32_t color)
	{
		Line(DirectX::XMFLOAT3(p.x - halfSize, p.y, p.z), DirectX::XMFLOAT3(p.x + halfSize, p.y, p.z), color);
		Line(DirectX::XMFLOAT3(p.x, p.y - halfSize, p.z), DirectX::XMFLOAT3(p.x, p.y + halfSize, p.z), color);
		Line(DirectX::XMFLOAT3(p.x, p.y, p.z - halfSize), DirectX::XMFLOAT3(p.x, p.y, p.z + halfSize), color);
	}

	// 변환 행렬(행벡터 관례)의 로컬 축 세 개를 X 빨강·Y 초록·Z 파랑으로 그림 (x → y → z 순). 스케일이 있어도 길이는 length 로 맞춤.
	void Axes(const DirectX::XMMATRIX& transform, float length)
	{
		using namespace DirectX;
		const uint32_t colors[3] = { PackLineColor(1.0f, 0.1f, 0.1f), PackLineColor(0.1f, 1.0f, 0.1f), PackLineColor(0.2f, 0.4f, 1.0f) };
		XMFLOAT3 origin;
		XMStoreFloat3(&origin, transform.r[3]);
		for (int axis = 0; axis < 3; ++axis)
		{
			const XMVECTOR direction = XMVector3Normalize(transform.r[axis]);
			XMFLOAT3 end;
			XMStoreFloat3(&end, XMVectorAdd(transform.r[3], XMVectorScale(direction, length)));
			Line(origin, end, colors[axis]);
		}
	}

private:
	std::vector<DebugLineVertex> m_vertices;
};

#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 스크린샷 (11단계). RHI Device 가 백버퍼를 CPU 로 읽어 준 RGBA8 픽셀을 PNG 로 저장함 (DirectXTex WIC).
// 화면 캡처와 달리 창이 가려져 있거나 화면 밖에 있어도 렌더링된 프레임을 정확히 얻음 — 자동 검증(--screenshot)용.
namespace Screenshot
{
	bool SavePng(const std::wstring& path, const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height);
}

#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Core/EngineApi.h"

// 게임 UI 글꼴 (docs/plan/ui-canvas-plan.html 2단계). stb_truetype 로 .ttf/.otf(CFF 포함)를 읽음. 가변 글꼴의 축은 읽지 않음
// (기본 인스턴스만) — 정적 글꼴을 쓸 것.
//
// 크기는 em 크기(px) — 유니티 Text.fontSize·CSS font-size 와 같은 뜻. 줄 높이 = (ascent − descent + lineGap) × 배율.
class SHERLOCK_API Font
{
public:
	Font();
	~Font();
	Font(const Font&) = delete;
	Font& operator=(const Font&) = delete;

	bool Load(const std::wstring& path, const std::string& name);
	const std::string& GetName() const { return m_name; }
	uint16_t GetId() const { return m_id; }
	void SetId(uint16_t id) { m_id = id; }

	int FindGlyph(uint32_t codepoint) const;          // 없으면 0 (.notdef)
	float GetScale(float pixelSize) const;            // em → px 배율
	void GetLineMetrics(float scale, float& ascent, float& descent, float& lineGap) const;   // px (descent 는 음수)
	float GetAdvance(int glyph, float scale) const;   // px
	float GetKerning(int left, int right, float scale) const;   // px (kern 표 또는 GPOS 쌍 조정)
	// 비트맵 상자: 펜 위치·기준선 기준 (y 아래로 +). 빈 글리프(공백)면 너비·높이 0
	void GetBitmapBox(int glyph, float scale, int& x0, int& y0, int& x1, int& y1) const;
	void Rasterize(int glyph, float scale, uint8_t* out, int width, int height, int stride) const;

private:
	struct Info;
	std::unique_ptr<Info> m_info;
	std::vector<uint8_t> m_data;   // stbtt 가 이 바이트를 계속 가리킴
	std::string m_name;
	uint16_t m_id = 0;
};

// 이름 → 글꼴. 이름 규칙은 텍스처와 같음: 빈 이름·"builtin:font" = 엔진 기본(Assets\Fonts\NotoSansKR-Regular.otf),
// "asset:<Assets 기준 경로>", 그 밖에는 Assets\Fonts\ 아래 파일 이름. 못 읽으면 기본 글꼴 (경고는 이름마다 한 번).
class SHERLOCK_API FontLibrary
{
public:
	FontLibrary() = default;
	FontLibrary(const FontLibrary&) = delete;
	FontLibrary& operator=(const FontLibrary&) = delete;
	static constexpr const char* kDefaultFont = "builtin:font";
	const Font* Get(const std::string& name);

private:
	std::unordered_map<std::string, std::unique_ptr<Font>> m_fonts;   // 실패한 이름은 nullptr 로 남겨 다시 읽지 않음
	uint16_t m_nextId = 1;
	bool m_defaultFailed = false;
};

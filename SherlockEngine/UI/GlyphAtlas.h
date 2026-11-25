#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>
#include "Core/EngineApi.h"
#include "UI/UIDrawList.h"

class Font;

// 글리프 아틀라스 (docs/plan/ui-canvas-plan.html 2단계, 3.2 "글리프 방식"). R8 한 장, 키 = (글꼴, 픽셀 크기, 글리프).
// 스카이라인 패킹(stb_rect_pack)으로 필요한 글자만 구움. 글리프 사이 여백 1 px.
//
// 가득 차면 Get 이 nullptr 를 돌려주고 Overflowed() 가 참이 됨. UISystem 이 그 프레임을 Grow(1024 → 2048) 또는 Clear(2048 에서)
// 뒤에 한 번 더 만듦 — 둘 다 버전이 바뀌어 Renderer 가 텍스처를 새로 만들므로, 앞 프레임이 읽는 텍스처를 덮어쓰지 않음.
// 새로 구운 글자는 image.MarkDirty 로 모아 두고 Renderer 가 올린 뒤 비움.
class SHERLOCK_API GlyphAtlas
{
public:
	struct Glyph
	{
		uint16_t x = 0, y = 0, width = 0, height = 0;   // 아틀라스 안 위치 (텍셀)
		int16_t offsetX = 0, offsetY = 0;                // 펜 위치·기준선 기준 비트맵 왼쪽 위 (y 아래로 +)
		bool tooLarge = false;                           // 가장 큰 아틀라스에도 들어가지 않는 글리프 → □ (넘침으로 치지 않음)
	};

	static constexpr uint32_t kInitialSize = 1024;
	static constexpr uint32_t kMaxSize = 2048;

	GlyphAtlas();
	~GlyphAtlas();
	GlyphAtlas(const GlyphAtlas&) = delete;
	GlyphAtlas& operator=(const GlyphAtlas&) = delete;

	// 없으면 구움. 가득 찼으면 nullptr (Overflowed). 빈 글리프(공백)는 크기 0 으로 성공.
	const Glyph* Get(const Font& font, uint32_t pixelSize, int glyph);
	bool Overflowed() const { return m_overflowed; }
	void ClearOverflow() { m_overflowed = false; }   // 다음 Get 이 빈자리를 다시 찾아봄
	uint32_t GetSize() const { return m_image.size; }
	size_t GetGlyphCount() const { return m_glyphs.size(); }

	void Grow();    // 다음 크기로 비우고 다시 시작 (가장 큰 크기면 같은 크기로 비움)
	void Clear();   // 같은 크기로 비움

	UIAtlasImage& GetImage() { return m_image; }

private:
	void Reset(uint32_t size);

	struct Packer;
	std::unique_ptr<Packer> m_packer;
	UIAtlasImage m_image;
	std::unordered_map<uint64_t, Glyph> m_glyphs;
	bool m_overflowed = false;
};

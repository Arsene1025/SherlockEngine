#include "pch.h"
#include "UI/GlyphAtlas.h"
#include <algorithm>
#include <vector>
#include <stb_rect_pack.h>   // 구현은 UI/Font.cpp
#include "UI/Font.h"

struct GlyphAtlas::Packer
{
	stbrp_context context = {};
	std::vector<stbrp_node> nodes;
};

GlyphAtlas::GlyphAtlas()
	: m_packer(std::make_unique<Packer>())
{
}

GlyphAtlas::~GlyphAtlas() = default;

void GlyphAtlas::Reset(uint32_t size)
{
	m_image.size = size;
	m_image.pixels.assign(static_cast<size_t>(size) * size, 0);
	++m_image.version;   // Renderer 가 텍스처를 새로 만들고 전체를 올림
	m_image.ClearDirty();
	m_glyphs.clear();
	m_packer->nodes.assign(size, stbrp_node{});
	stbrp_init_target(&m_packer->context, static_cast<int>(size), static_cast<int>(size), m_packer->nodes.data(), static_cast<int>(size));
	m_overflowed = false;
}

void GlyphAtlas::Grow()
{
	Reset((std::min)(m_image.size == 0 ? kInitialSize : m_image.size * 2, kMaxSize));
}

void GlyphAtlas::Clear()
{
	Reset(m_image.size == 0 ? kInitialSize : m_image.size);
}

const GlyphAtlas::Glyph* GlyphAtlas::Get(const Font& font, uint32_t pixelSize, int glyph)
{
	if (m_image.size == 0)
	{
		Reset(kInitialSize);
	}
	const uint64_t key = (static_cast<uint64_t>(font.GetId()) << 48) | (static_cast<uint64_t>(pixelSize & 0xFFFF) << 32) | static_cast<uint32_t>(glyph);
	const auto found = m_glyphs.find(key);
	if (found != m_glyphs.end()) return &found->second;
	if (m_overflowed) return nullptr;   // 이번 프레임은 이미 넘침 — 다시 만들 때까지 더 굽지 않음

	const float scale = font.GetScale(static_cast<float>(pixelSize));
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	font.GetBitmapBox(glyph, scale, x0, y0, x1, y1);
	Glyph g;
	g.offsetX = static_cast<int16_t>(x0);
	g.offsetY = static_cast<int16_t>(y0);
	const int w = x1 - x0;
	const int h = y1 - y0;
	if (w + 1 > static_cast<int>(kMaxSize) || h + 1 > static_cast<int>(kMaxSize))
	{
		g.tooLarge = true;   // 비우고 키워도 못 넣음 — 넘침으로 치면 매번 아틀라스를 비우게 됨
		return &m_glyphs.emplace(key, g).first->second;
	}
	if (w > 0 && h > 0)
	{
		stbrp_rect rect = {};
		rect.w = w + 1;   // 오른쪽·아래 1 px 여백 (아틀라스는 0 으로 시작)
		rect.h = h + 1;
		if (!stbrp_pack_rects(&m_packer->context, &rect, 1) || !rect.was_packed)
		{
			m_overflowed = true;
			return nullptr;
		}
		g.x = static_cast<uint16_t>(rect.x);
		g.y = static_cast<uint16_t>(rect.y);
		g.width = static_cast<uint16_t>(w);
		g.height = static_cast<uint16_t>(h);
		font.Rasterize(glyph, scale, m_image.pixels.data() + static_cast<size_t>(g.y) * m_image.size + g.x, w, h, static_cast<int>(m_image.size));
		m_image.MarkDirty(g.x, g.y, g.width, g.height);
	}
	return &m_glyphs.emplace(key, g).first->second;
}

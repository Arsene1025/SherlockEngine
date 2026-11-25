#include "pch.h"
#include "UI/Font.h"
#include <cmath>
#include <fstream>
#include "Core/Log.h"
#include "Core/Paths.h"

// stb 구현은 이 파일 하나에서 정의 (vcpkg stb 포트). imgui 는 자기 사본을 static 으로 넣으므로 이름이 겹치지 않음.
#define STB_TRUETYPE_IMPLEMENTATION
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>
#include <stb_truetype.h>

struct Font::Info
{
	stbtt_fontinfo font = {};
};

Font::Font()
	: m_info(std::make_unique<Info>())
{
}

Font::~Font() = default;

bool Font::Load(const std::wstring& path, const std::string& name)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) return false;
	m_data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	if (m_data.size() < 12) return false;
	const int offset = stbtt_GetFontOffsetForIndex(m_data.data(), 0);
	if (offset < 0 || !stbtt_InitFont(&m_info->font, m_data.data(), offset)) return false;
	m_name = name;
	return true;
}

int Font::FindGlyph(uint32_t codepoint) const
{
	return stbtt_FindGlyphIndex(&m_info->font, static_cast<int>(codepoint));
}

float Font::GetScale(float pixelSize) const
{
	return stbtt_ScaleForMappingEmToPixels(&m_info->font, pixelSize);
}

void Font::GetLineMetrics(float scale, float& ascent, float& descent, float& lineGap) const
{
	int a = 0, d = 0, g = 0;
	stbtt_GetFontVMetrics(&m_info->font, &a, &d, &g);
	ascent = a * scale;
	descent = d * scale;
	lineGap = g * scale;
}

float Font::GetAdvance(int glyph, float scale) const
{
	int advance = 0, bearing = 0;
	stbtt_GetGlyphHMetrics(&m_info->font, glyph, &advance, &bearing);
	return advance * scale;
}

float Font::GetKerning(int left, int right, float scale) const
{
	return stbtt_GetGlyphKernAdvance(&m_info->font, left, right) * scale;
}

void Font::GetBitmapBox(int glyph, float scale, int& x0, int& y0, int& x1, int& y1) const
{
	stbtt_GetGlyphBitmapBox(&m_info->font, glyph, scale, scale, &x0, &y0, &x1, &y1);
}

void Font::Rasterize(int glyph, float scale, uint8_t* out, int width, int height, int stride) const
{
	stbtt_MakeGlyphBitmap(&m_info->font, out, width, height, stride, scale, scale, glyph);
}

// ------------------------------------------------------------------ FontLibrary

const Font* FontLibrary::Get(const std::string& nameIn)
{
	const std::string name = nameIn.empty() ? std::string(kDefaultFont) : nameIn;
	const auto found = m_fonts.find(name);
	if (found != m_fonts.end())
	{
		if (found->second) return found->second.get();
		return name == kDefaultFont ? nullptr : Get(kDefaultFont);   // 실패한 이름 → 기본
	}
	auto font = std::make_unique<Font>();
	// UTF-8 이름을 넓은 문자로 (경로에 한글이 있을 수 있음)
	std::wstring path;
	{
		const int count = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
		std::wstring wide(count > 0 ? count - 1 : 0, L'\0');
		if (count > 1)
		{
			MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wide.data(), count);
		}
		if (name == kDefaultFont)
		{
			path = Paths::GetAssetPath(L"Fonts\\NotoSansKR-Regular.otf");
		}
		else if (name.rfind("asset:", 0) == 0)
		{
			path = Paths::GetAssetPath(wide.substr(6).c_str());   // "asset:" 은 ASCII 6 글자
		}
		else
		{
			path = Paths::GetAssetPath((L"Fonts\\" + wide).c_str());
		}
	}
	if (!font->Load(path, name))
	{
		if (name == kDefaultFont)
		{
			if (!m_defaultFailed)
			{
				Log::Error("UI: 기본 글꼴을 읽지 못함: %s (글자를 그리지 않음)", Log::ToUtf8(path.c_str()).c_str());
				m_defaultFailed = true;
			}
			m_fonts.emplace(name, nullptr);
			return nullptr;
		}
		Log::Warn("UI: 글꼴 '%s' 를 읽지 못해 기본 글꼴로 대체 (%s)", name.c_str(), Log::ToUtf8(path.c_str()).c_str());
		m_fonts.emplace(name, nullptr);
		return Get(kDefaultFont);
	}
	font->SetId(m_nextId++);
	Log::Info("UI: 글꼴 읽음 %s (%s)", name.c_str(), Log::ToUtf8(path.c_str()).c_str());
	const Font* result = font.get();
	m_fonts.emplace(name, std::move(font));
	return result;
}

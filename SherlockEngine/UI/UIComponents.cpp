#include "pch.h"
#include "UI/UIComponents.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include "UI/UISystem.h"

using namespace DirectX;   // 이 파일 안에서만

// ------------------------------------------------------------------ Canvas

namespace
{
	const char* const kRenderModes[] = { "Screen Space Overlay", "World Space" };
}

void Canvas::Reflect(PropertyVisitor& v)
{
	v.Enum("renderMode", renderMode, kRenderModes, 2);
	v.Int("sortOrder", sortOrder);
	v.Float("pixelsPerUnit", pixelsPerUnit, 0.01f);
}

SHERLOCK_BEHAVIOUR(Canvas)

// ------------------------------------------------------------------ CanvasScaler

namespace
{
	const char* const kScalerModes[] = { "Constant Pixel Size", "Scale With Screen Size" };
}

void CanvasScaler::Reflect(PropertyVisitor& v)
{
	v.Enum("mode", mode, kScalerModes, 2);
	v.Float("scaleFactor", scaleFactor, 0.01f);
	v.Float2("referenceResolution", referenceResolution, 1.0f);
	v.Float("match", match, 0.01f);
}

float CanvasScaler::ComputeScale(float screenWidth, float screenHeight) const
{
	if (mode == ConstantPixelSize)
	{
		return scaleFactor > 1e-4f ? scaleFactor : 1e-4f;
	}
	if (screenWidth <= 0.0f || screenHeight <= 0.0f || referenceResolution.x <= 0.0f || referenceResolution.y <= 0.0f)
	{
		return 1.0f;
	}
	// 로그 공간에서 섞어야 가로로 긴 화면과 세로로 긴 화면이 대칭이 됨 (유니티 CanvasScaler.HandleScaleWithScreenSize)
	const float logW = std::log2(screenWidth / referenceResolution.x);
	const float logH = std::log2(screenHeight / referenceResolution.y);
	const float t = (std::clamp)(match, 0.0f, 1.0f);
	return std::exp2(logW + (logH - logW) * t);
}

SHERLOCK_BEHAVIOUR(CanvasScaler)

// ------------------------------------------------------------------ RectTransform

void RectTransform::Reflect(PropertyVisitor& v)
{
	v.Float2("anchorMin", anchorMin, 0.01f);
	v.Float2("anchorMax", anchorMax, 0.01f);
	v.Float2("pivot", pivot, 0.01f);
	v.Float2("anchoredPosition", anchoredPosition, 1.0f);
	v.Float2("sizeDelta", sizeDelta, 1.0f);
}

UIRect RectTransform::ComputeRect(const UIRect& parent, XMFLOAT2* pivotPoint) const
{
	return ComputeRectWithAnchors(parent, anchorMin, anchorMax, pivotPoint);
}

UIRect RectTransform::ComputeRectWithAnchors(const UIRect& parent, const XMFLOAT2& minAnchor, const XMFLOAT2& maxAnchor, XMFLOAT2* pivotPoint) const
{
	// docs/plan/ui-canvas-plan.html 2.1
	const float pw = parent.Width();
	const float ph = parent.Height();
	const XMFLOAT2 aMin(parent.min.x + minAnchor.x * pw, parent.min.y + minAnchor.y * ph);
	const XMFLOAT2 aMax(parent.min.x + maxAnchor.x * pw, parent.min.y + maxAnchor.y * ph);
	const XMFLOAT2 size((aMax.x - aMin.x) + sizeDelta.x, (aMax.y - aMin.y) + sizeDelta.y);
	const XMFLOAT2 pivotP(aMin.x + (aMax.x - aMin.x) * pivot.x + anchoredPosition.x, aMin.y + (aMax.y - aMin.y) * pivot.y + anchoredPosition.y);
	UIRect rect;
	rect.min = XMFLOAT2(pivotP.x - size.x * pivot.x, pivotP.y - size.y * pivot.y);
	rect.max = XMFLOAT2(rect.min.x + size.x, rect.min.y + size.y);
	if (pivotPoint != nullptr)
	{
		*pivotPoint = pivotP;
	}
	return rect;
}

SHERLOCK_BEHAVIOUR(RectTransform)

// ------------------------------------------------------------------ UIGraphic / UIImage

void UIGraphic::ReflectGraphic(PropertyVisitor& v)
{
	v.Color("color", color);
	v.Bool("raycastTarget", raycastTarget);
}

namespace
{
	const char* const kImageTypes[] = { "Simple", "Sliced", "Filled" };
	const char* const kFillMethods[] = { "Horizontal", "Vertical" };
}

void UIImage::Reflect(PropertyVisitor& v)
{
	ReflectGraphic(v);
	v.Asset("texture", texture, PropertyVisitor::AssetKind::Texture);
	v.Enum("imageType", type, kImageTypes, 3);   // "type" 은 컴포넌트 JSON 의 타입 이름 키라 쓰면 안 됨 (SceneSerializer)
	v.Bool("preserveAspect", preserveAspect);
	v.Float("borderLeft", borderLeft, 1.0f);
	v.Float("borderRight", borderRight, 1.0f);
	v.Float("borderTop", borderTop, 1.0f);
	v.Float("borderBottom", borderBottom, 1.0f);
	v.Bool("fillCenter", fillCenter);
	v.Enum("fillMethod", fillMethod, kFillMethods, 2);
	v.Int("fillOrigin", fillOrigin);
	v.Float("fillAmount", fillAmount, 0.01f);
}

void UIImage::Populate(UIMeshBuilder& builder, const UIRect& rect, const UIAffine& toCanvas) const
{
	if (rect.Width() <= 0.0f || rect.Height() <= 0.0f || color.w <= 0.0f) return;
	const uint32_t packed = PackUIColor(color);
	uint32_t tw = 0, th = 0;
	const bool hasSize = !texture.empty() && builder.GetTextureSize(texture, tw, th) && tw > 0 && th > 0;

	if (type == Sliced && hasSize)
	{
		const XMFLOAT4 border = GetBorder();
		const float bl = (std::max)(0.0f, border.x), br = (std::max)(0.0f, border.y), bt = (std::max)(0.0f, border.z), bb = (std::max)(0.0f, border.w);
		if (bl + br + bt + bb > 0.0f)
		{
			// 사각형이 경계 합보다 작으면 두 경계를 같은 비율로 줄임 (유니티 Image.GenerateSlicedSprite 와 같은 규칙)
			const float w = rect.Width();
			const float h = rect.Height();
			const float sx = bl + br > w ? w / (bl + br) : 1.0f;
			const float sy = bt + bb > h ? h / (bt + bb) : 1.0f;
			const float xs[4] = { rect.min.x, rect.min.x + bl * sx, rect.max.x - br * sx, rect.max.x };
			const float ys[4] = { rect.min.y, rect.min.y + bb * sy, rect.max.y - bt * sy, rect.max.y };
			const float us[4] = { 0.0f, bl / tw, 1.0f - br / tw, 1.0f };
			const float vs[4] = { 1.0f, 1.0f - bb / th, bt / th, 0.0f };   // 아래 → 위 (텍스처 v 는 아래가 1)
			for (int y = 0; y < 3; ++y)
			{
				for (int x = 0; x < 3; ++x)
				{
					if (x == 1 && y == 1 && !fillCenter) continue;
					if (xs[x + 1] - xs[x] <= 0.0f || ys[y + 1] - ys[y] <= 0.0f) continue;
					UIRect cell;
					cell.min = XMFLOAT2(xs[x], ys[y]);
					cell.max = XMFLOAT2(xs[x + 1], ys[y + 1]);
					builder.AddQuad(cell, toCanvas, XMFLOAT4(us[x], vs[y + 1], us[x + 1], vs[y]), packed, texture);
				}
			}
			return;
		}
	}

	UIRect r = rect;
	XMFLOAT4 uv(0.0f, 0.0f, 1.0f, 1.0f);
	if (type == Filled)
	{
		const float amount = (std::clamp)(fillAmount, 0.0f, 1.0f);
		if (amount <= 0.0f) return;
		if (fillMethod == Horizontal)
		{
			if (fillOrigin == 0)
			{
				r.max.x = r.min.x + rect.Width() * amount;
				uv.z = amount;
			}
			else
			{
				r.min.x = r.max.x - rect.Width() * amount;
				uv.x = 1.0f - amount;
			}
		}
		else
		{
			if (fillOrigin == 0)
			{
				r.max.y = r.min.y + rect.Height() * amount;
				uv.y = 1.0f - amount;   // 아래부터 — 위쪽 v 를 내림
			}
			else
			{
				r.min.y = r.max.y - rect.Height() * amount;
				uv.w = amount;
			}
		}
	}
	else if (preserveAspect && hasSize)
	{
		// 텍스처 비율을 지키며 사각형 안에 가장 크게, 가운데 (유니티는 피벗 기준 — 여기서는 가운데)
		const float aspect = static_cast<float>(tw) / static_cast<float>(th);
		const float w = rect.Width();
		const float h = rect.Height();
		if (w / h > aspect)
		{
			const float nw = h * aspect;
			r.min.x = rect.min.x + (w - nw) * 0.5f;
			r.max.x = r.min.x + nw;
		}
		else
		{
			const float nh = w / aspect;
			r.min.y = rect.min.y + (h - nh) * 0.5f;
			r.max.y = r.min.y + nh;
		}
	}
	builder.AddQuad(r, toCanvas, uv, packed, texture);
}

XMFLOAT2 UIImage::GetPreferredSize(UIMeshBuilder& builder, float width) const
{
	(void)width;
	uint32_t tw = 0, th = 0;
	if (texture.empty() || !builder.GetTextureSize(texture, tw, th)) return XMFLOAT2(0.0f, 0.0f);
	if (type == Sliced)
	{
		const XMFLOAT4 b = GetBorder();
		return XMFLOAT2(b.x + b.y, b.z + b.w);
	}
	return XMFLOAT2(static_cast<float>(tw), static_cast<float>(th));
}

SHERLOCK_BEHAVIOUR(UIImage)

// ------------------------------------------------------------------ UIText

namespace
{
	const char* const kAlignmentNames[] = { "Upper Left", "Upper Center", "Upper Right", "Middle Left", "Middle Center", "Middle Right", "Lower Left", "Lower Center", "Lower Right" };
	const char* const kVerticalOverflowNames[] = { "Overflow", "Truncate", "Ellipsis" };

	// UTF-8 → 코드포인트. 잘못된 바이트는 U+FFFD
	void DecodeUtf8(const std::string& text, std::vector<uint32_t>& out)
	{
		out.clear();
		const unsigned char* p = reinterpret_cast<const unsigned char*>(text.data());
		const unsigned char* end = p + text.size();
		while (p < end)
		{
			uint32_t c = *p++;
			int extra = 0;
			if (c >= 0xF0) { c &= 0x07; extra = 3; }
			else if (c >= 0xE0) { c &= 0x0F; extra = 2; }
			else if (c >= 0xC0) { c &= 0x1F; extra = 1; }
			else if (c >= 0x80) { out.push_back(0xFFFD); continue; }
			bool ok = true;
			for (int i = 0; i < extra; ++i)
			{
				if (p >= end || (*p & 0xC0) != 0x80)
				{
					ok = false;
					break;
				}
				c = (c << 6) | (*p++ & 0x3F);
			}
			out.push_back(ok ? c : 0xFFFD);
		}
	}

	struct TextItem
	{
		uint32_t codepoint = 0;
		int glyph = 0;          // 0 = 글꼴에 없음 → □
		float advance = 0.0f;   // px (커닝 제외)
	};

	struct TextLine
	{
		size_t begin, end;
	};

	// 글자 배치 (Populate·GetPreferredSize 공용). 픽셀 단위.
	struct TextLayout
	{
		const Font* font = nullptr;
		uint32_t pixelSize = 0;
		float fontScale = 0.0f;
		float ascent = 0.0f, descent = 0.0f, lineGap = 0.0f;
		float lineHeight = 0.0f;
		float boxWidth = 0.0f;   // 글꼴에 없는 글자 □ 의 너비
		std::vector<TextItem> items;
		std::vector<TextLine> lines;

		float Kern(size_t i) const
		{
			return (i > 0 && items[i - 1].glyph != 0 && items[i].glyph != 0) ? font->GetKerning(items[i - 1].glyph, items[i].glyph, fontScale) : 0.0f;
		}
		float Measure(size_t begin, size_t end) const
		{
			while (end > begin && items[end - 1].codepoint == ' ')
			{
				--end;   // 줄 끝 공백은 너비에 넣지 않음 (정렬)
			}
			float width = 0.0f;
			for (size_t i = begin; i < end; ++i)
			{
				width += items[i].advance + (i > begin ? Kern(i) : 0.0f);
			}
			return width;
		}
		float FirstLine() const { return ascent - descent; }   // 첫 줄 높이 (줄 간격 없이)
	};

	bool BuildTextLayout(const UIText& t, UIMeshBuilder& builder, float maxWidth, bool wrap, TextLayout& out)
	{
		if (t.text.empty() || t.fontSize <= 0) return false;
		const Font* fontPtr = builder.GetFonts().Get(t.font);
		if (fontPtr == nullptr) return false;
		const Font& f = *fontPtr;
		out.font = fontPtr;
		const float scale = builder.GetScale();
		out.pixelSize = static_cast<uint32_t>((std::max)(1.0f, std::round(static_cast<float>(t.fontSize) * scale)));
		out.fontScale = f.GetScale(static_cast<float>(out.pixelSize));
		f.GetLineMetrics(out.fontScale, out.ascent, out.descent, out.lineGap);
		out.lineHeight = (out.ascent - out.descent + out.lineGap) * (std::max)(0.0f, t.lineSpacing);
		out.boxWidth = std::round(out.pixelSize * 0.5f);

		std::vector<uint32_t> codepoints;
		DecodeUtf8(t.text, codepoints);
		out.items.clear();
		out.items.reserve(codepoints.size());
		for (uint32_t cp : codepoints)
		{
			if (cp == '\r') continue;
			TextItem item;
			item.codepoint = cp;
			if (cp == '\n')
			{
				out.items.push_back(item);
				continue;
			}
			if (cp == '\t')
			{
				item.codepoint = ' ';
			}
			item.glyph = f.FindGlyph(item.codepoint);
			item.advance = item.glyph != 0 ? f.GetAdvance(item.glyph, out.fontScale) : out.boxWidth + std::round(out.pixelSize * 0.1f);
			out.items.push_back(item);
		}

		// 줄 나누기
		const std::vector<TextItem>& items = out.items;
		std::vector<TextLine>& lines = out.lines;
		lines.clear();
		size_t lineStart = 0;
		size_t lastSpace = SIZE_MAX;
		float width = 0.0f;
		for (size_t i = 0; i < items.size(); ++i)
		{
			const uint32_t cp = items[i].codepoint;
			if (cp == '\n')
			{
				lines.push_back({ lineStart, i });
				lineStart = i + 1;
				lastSpace = SIZE_MAX;
				width = 0.0f;
				continue;
			}
			const float advance = items[i].advance + (i > lineStart ? out.Kern(i) : 0.0f);
			if (wrap && cp != ' ' && i > lineStart && width + advance > maxWidth + 0.01f)
			{
				if (lastSpace != SIZE_MAX && lastSpace > lineStart)
				{
					lines.push_back({ lineStart, lastSpace });   // 마지막 공백에서 끊음
					lineStart = lastSpace + 1;
				}
				else
				{
					lines.push_back({ lineStart, i });   // 한 낱말이 줄보다 김 → 글자 단위
					lineStart = i;
				}
				lastSpace = SIZE_MAX;
				width = out.Measure(lineStart, i + 1);
				if (width > maxWidth + 0.01f && i > lineStart)
				{
					// 공백에서 끊고 넘어온 조각 + 이 글자가 이미 줄보다 김 → 이 글자 앞에서 한 번 더 끊음
					lines.push_back({ lineStart, i });
					lineStart = i;
					width = out.Measure(i, i + 1);
				}
				continue;
			}
			if (cp == ' ')
			{
				lastSpace = i;
			}
			width += advance;
		}
		lines.push_back({ lineStart, items.size() });
		return true;
	}
}

UIText::UIText()
{
	color = XMFLOAT4(0.196f, 0.196f, 0.196f, 1.0f);   // 유니티 Text 기본 (50, 50, 50)
}

void UIText::Reflect(PropertyVisitor& v)
{
	ReflectGraphic(v);
	v.MultilineString("text", text);
	v.Asset("font", font, PropertyVisitor::AssetKind::Font);
	v.Int("fontSize", fontSize);
	v.Enum("alignment", alignment, kAlignmentNames, 9);
	v.Float("lineSpacing", lineSpacing, 0.01f);
	v.Bool("wrap", wrap);
	v.Enum("verticalOverflow", verticalOverflow, kVerticalOverflowNames, 3);
}

XMFLOAT2 UIText::GetPreferredSize(UIMeshBuilder& builder, float width) const
{
	const float scale = builder.GetScale();
	XMFLOAT2 size(0.0f, 0.0f);
	TextLayout layout;
	if (!BuildTextLayout(*this, builder, 0.0f, false, layout)) return size;
	float widest = 0.0f;
	for (const TextLine& line : layout.lines)
	{
		widest = (std::max)(widest, layout.Measure(line.begin, line.end));
	}
	size.x = std::ceil(widest) / scale;
	if (wrap)
	{
		BuildTextLayout(*this, builder, width * scale, true, layout);   // 세로 = width 로 줄바꿈했을 때의 줄 수
	}
	const float height = layout.FirstLine() + layout.lineHeight * static_cast<float>(layout.lines.size() - 1);
	size.y = std::ceil(height) / scale;
	return size;
}

void UIText::Populate(UIMeshBuilder& builder, const UIRect& rect, const UIAffine& toCanvas) const
{
	if (color.w <= 0.0f) return;
	const float scale = builder.GetScale();
	const float maxWidth = rect.Width() * scale;
	TextLayout layout;
	if (!BuildTextLayout(*this, builder, maxWidth, wrap, layout)) return;
	const Font& f = *layout.font;
	GlyphAtlas& atlas = builder.GetAtlas();
	const uint32_t pixelSize = layout.pixelSize;
	const float fontScale = layout.fontScale;
	const float ascent = layout.ascent;
	const float lineHeight = layout.lineHeight;
	const float boxWidth = layout.boxWidth;
	const uint32_t packed = PackUIColor(color);
	const std::vector<TextItem>& items = layout.items;
	std::vector<TextLine>& lines = layout.lines;
	const auto kern = [&](size_t i) { return layout.Kern(i); };
	const auto measure = [&](size_t begin, size_t end) { return layout.Measure(begin, end); };

	// 세로: 줄 수가 사각형에 다 들어가는가 (Truncate·Ellipsis)
	const float boxHeight = rect.Height() * scale;
	const float firstLine = layout.FirstLine();
	size_t visible = lines.size();
	bool truncated = false;
	if (verticalOverflow != Overflow)
	{
		const size_t fit = boxHeight + 0.01f < firstLine ? 0 : static_cast<size_t>(std::floor((boxHeight - firstLine) / (std::max)(lineHeight, 1e-3f) + 1e-4f)) + 1;
		if (fit < visible)
		{
			visible = fit;
			truncated = true;
		}
	}
	if (visible == 0) return;

	// 말줄임: 마지막 보이는 줄 끝에서 글자를 빼며 "…" 가 들어갈 자리를 만듦
	std::vector<TextItem> ellipsisItems;
	if (truncated && verticalOverflow == Ellipsis)
	{
		const int dots = f.FindGlyph(0x2026);
		if (dots != 0)
		{
			ellipsisItems.push_back({ 0x2026, dots, f.GetAdvance(dots, fontScale) });
		}
		else
		{
			const int dot = f.FindGlyph('.');
			for (int i = 0; i < 3; ++i)
			{
				ellipsisItems.push_back({ '.', dot, f.GetAdvance(dot, fontScale) });
			}
		}
		float ellipsisWidth = 0.0f;
		for (const TextItem& e : ellipsisItems)
		{
			ellipsisWidth += e.advance;
		}
		TextLine& last = lines[visible - 1];
		while (last.end > last.begin && (measure(last.begin, last.end) + ellipsisWidth > maxWidth || items[last.end - 1].codepoint == ' '))
		{
			--last.end;
		}
	}

	// 블록 위치. 캔버스 y 위 + — 첫 기준선 = 블록 위 − ascent.
	const float blockHeight = firstLine + lineHeight * static_cast<float>(visible - 1);
	const int vertical = alignment / 3;     // 0 위, 1 가운데, 2 아래
	const int horizontal = alignment % 3;   // 0 왼쪽, 1 가운데, 2 오른쪽
	const float left = rect.min.x * scale;
	const float top = rect.max.y * scale;
	float blockTop = top;
	if (vertical == 1)
	{
		blockTop = top - (boxHeight - blockHeight) * 0.5f;
	}
	else if (vertical == 2)
	{
		blockTop = rect.min.y * scale + blockHeight;
	}

	// 화면 픽셀 격자: 로컬 → 캔버스가 이동뿐이고 화면 공간 캔버스이면 (로컬 px + 이동 px) 가 정수가 되게 맞춤
	const bool snap = builder.AllowsPixelSnap() && toCanvas.a == 1.0f && toCanvas.b == 0.0f && toCanvas.c == 0.0f && toCanvas.d == 1.0f;
	const auto snapX = [&](float x) { return snap ? std::round(x + toCanvas.tx * scale) - toCanvas.tx * scale : x; };
	const auto snapY = [&](float y) { return snap ? std::round(y + toCanvas.ty * scale) - toCanvas.ty * scale : y; };
	const float inv = 1.0f / scale;
	const auto addPixelQuad = [&](float x0, float y0, float x1, float y1, const XMFLOAT4& uv, const std::string& texture, bool glyph)
	{
		UIRect r;
		r.min = XMFLOAT2(x0 * inv, y0 * inv);
		r.max = XMFLOAT2(x1 * inv, y1 * inv);
		builder.AddQuad(r, toCanvas, uv, packed, texture, glyph);
	};
	const auto drawBox = [&](float penX, float baseline)
	{
		// 글꼴에 없거나 아틀라스에 못 넣은 글자: 흰 텍스처 네 변으로 □
		const float t = (std::max)(1.0f, std::round(pixelSize / 16.0f));
		const float x0 = penX + std::round(pixelSize * 0.05f);
		const float x1 = x0 + boxWidth;
		const float y0 = baseline;
		const float y1 = baseline + std::round(ascent * 0.75f);
		const XMFLOAT4 uv(0.0f, 0.0f, 1.0f, 1.0f);
		addPixelQuad(x0, y0, x1, y0 + t, uv, std::string(), false);
		addPixelQuad(x0, y1 - t, x1, y1, uv, std::string(), false);
		addPixelQuad(x0, y0 + t, x0 + t, y1 - t, uv, std::string(), false);
		addPixelQuad(x1 - t, y0 + t, x1, y1 - t, uv, std::string(), false);
	};
	const auto drawItem = [&](const TextItem& item, float penX, float baseline)
	{
		if (item.codepoint == ' ') return;
		if (item.glyph == 0)
		{
			drawBox(penX, baseline);
			return;
		}
		const GlyphAtlas::Glyph* g = atlas.Get(f, pixelSize, item.glyph);
		if (g == nullptr)
		{
			if (builder.IsFinalPass())
			{
				builder.ReportGlyphOverflow();
				drawBox(penX, baseline);
			}
			return;   // 첫 시도: 아틀라스를 키운 뒤 다시 만듦
		}
		if (g->tooLarge)
		{
			drawBox(penX, baseline);
			return;
		}
		if (g->width == 0 || g->height == 0) return;
		const float size = static_cast<float>(atlas.GetSize());
		const float x0 = snapX(penX) + g->offsetX;   // 글자마다 시작을 화면 픽셀에 (펜은 실수로 쌓음)
		const float y1 = baseline - g->offsetY;   // 비트맵 위 (y 위 +)
		const XMFLOAT4 uv(g->x / size, g->y / size, (g->x + g->width) / size, (g->y + g->height) / size);
		addPixelQuad(x0, y1 - g->height, x0 + g->width, y1, uv, kUIGlyphAtlasTexture, true);
	};

	for (size_t l = 0; l < visible; ++l)
	{
		const TextLine& line = lines[l];
		const bool withEllipsis = l + 1 == visible && !ellipsisItems.empty();
		float width = measure(line.begin, line.end);
		if (withEllipsis)
		{
			for (const TextItem& e : ellipsisItems)
			{
				width += e.advance;
			}
		}
		float x = left;
		if (horizontal == 1)
		{
			x = left + (maxWidth - width) * 0.5f;
		}
		else if (horizontal == 2)
		{
			x = left + maxWidth - width;
		}
		const float lineX = snapX(x);
		const float baseline = snapY(blockTop - ascent - lineHeight * static_cast<float>(l));
		float pen = 0.0f;
		for (size_t i = line.begin; i < line.end; ++i)
		{
			pen += i > line.begin ? kern(i) : 0.0f;
			drawItem(items[i], lineX + pen, baseline);
			pen += items[i].advance;
		}
		if (withEllipsis)
		{
			for (const TextItem& e : ellipsisItems)
			{
				drawItem(e, lineX + pen, baseline);
				pen += e.advance;
			}
		}
	}
}

SHERLOCK_BEHAVIOUR(UIText)

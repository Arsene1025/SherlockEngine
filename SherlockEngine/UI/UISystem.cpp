#include "pch.h"
#include "UI/UISystem.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include "Core/Input.h"
#include "Core/Log.h"
#include "Scene/Camera.h"
#include "Scene/GameObject.h"
#include "Scene/Scene.h"
#include "UI/UIComponents.h"
#include "UI/UIEvent.h"
#include "UI/UILayout.h"
#include "UI/UIWidgets.h"

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	// 마스크가 없을 때의 클립 사각형
	constexpr float kNoClip = 1.0e7f;
	constexpr float kDragThreshold = 10.0f;   // ScrollRect: 이만큼(출력 픽셀) 움직이면 드래그 — 누른 버튼의 클릭은 취소

	UIRect NoClip()
	{
		UIRect r;
		r.min = XMFLOAT2(-kNoClip, -kNoClip);
		r.max = XMFLOAT2(kNoClip, kNoClip);
		return r;
	}

	UIRect Intersect(const UIRect& a, const UIRect& b)
	{
		UIRect r;
		r.min = XMFLOAT2((std::max)(a.min.x, b.min.x), (std::max)(a.min.y, b.min.y));
		r.max = XMFLOAT2((std::min)(a.max.x, b.max.x), (std::min)(a.max.y, b.max.y));
		return r;
	}

	// 요소 사각형을 캔버스로 옮긴 축 정렬 상자
	UIRect CanvasBounds(const UIRect& rect, const UIAffine& toCanvas)
	{
		const XMFLOAT2 corners[4] = { rect.min, XMFLOAT2(rect.max.x, rect.min.y), rect.max, XMFLOAT2(rect.min.x, rect.max.y) };
		UIRect r;
		r.min = XMFLOAT2(kNoClip, kNoClip);
		r.max = XMFLOAT2(-kNoClip, -kNoClip);
		for (const XMFLOAT2& c : corners)
		{
			const XMFLOAT2 p = toCanvas.Apply(c);
			r.min = XMFLOAT2((std::min)(r.min.x, p.x), (std::min)(r.min.y, p.y));
			r.max = XMFLOAT2((std::max)(r.max.x, p.x), (std::max)(r.max.y, p.y));
		}
		return r;
	}

	XMFLOAT4 LerpColor(const XMFLOAT4& a, const XMFLOAT4& b, float t)
	{
		return XMFLOAT4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
	}

	bool SameColor(const XMFLOAT4& a, const XMFLOAT4& b)
	{
		return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
	}

	float Component(const XMFLOAT2& v, int axis) { return axis == 0 ? v.x : v.y; }
	float& Component(XMFLOAT2& v, int axis) { return axis == 0 ? v.x : v.y; }

	// 오브젝트의 켜진 UI 컴포넌트 (dynamic_cast 한 번씩)
	struct UIParts
	{
		const RectTransform* rect = nullptr;
		const UIGraphic* graphics[4] = {};
		int graphicCount = 0;
		const UISelectable* selectable = nullptr;
		const UIToggle* toggle = nullptr;
		const UISlider* slider = nullptr;
		const UIScrollbar* scrollbar = nullptr;
		const UIScrollRect* scroll = nullptr;
		const UILayoutGroup* group = nullptr;
		const UILayoutElement* element = nullptr;
		const UIContentSizeFitter* fitter = nullptr;
		const UIRectMask* mask = nullptr;
	};

	UIParts Gather(const GameObject& object)
	{
		UIParts parts;
		for (const auto& b : object.GetBehaviours())
		{
			const UIBehaviour* ui = dynamic_cast<const UIBehaviour*>(b.get());
			if (ui == nullptr || !ui->enabled) continue;
			switch (ui->GetUIKind())
			{
			case UIBehaviour::KindRectTransform:
				parts.rect = parts.rect != nullptr ? parts.rect : static_cast<const RectTransform*>(ui);
				break;
			case UIBehaviour::KindGraphic:
				if (parts.graphicCount < 4)
				{
					parts.graphics[parts.graphicCount++] = static_cast<const UIGraphic*>(ui);
				}
				break;
			case UIBehaviour::KindButton:
				parts.selectable = parts.selectable != nullptr ? parts.selectable : static_cast<const UISelectable*>(ui);
				break;
			case UIBehaviour::KindToggle:
				parts.selectable = parts.selectable != nullptr ? parts.selectable : static_cast<const UISelectable*>(ui);
				parts.toggle = static_cast<const UIToggle*>(ui);
				break;
			case UIBehaviour::KindSlider:
				parts.selectable = parts.selectable != nullptr ? parts.selectable : static_cast<const UISelectable*>(ui);
				parts.slider = static_cast<const UISlider*>(ui);
				break;
			case UIBehaviour::KindScrollbar:
				parts.selectable = parts.selectable != nullptr ? parts.selectable : static_cast<const UISelectable*>(ui);
				parts.scrollbar = static_cast<const UIScrollbar*>(ui);
				break;
			case UIBehaviour::KindScrollRect:
				parts.scroll = static_cast<const UIScrollRect*>(ui);
				break;
			case UIBehaviour::KindLayoutGroup:
				parts.group = parts.group != nullptr ? parts.group : static_cast<const UILayoutGroup*>(ui);
				break;
			case UIBehaviour::KindLayoutElement:
				parts.element = static_cast<const UILayoutElement*>(ui);
				break;
			case UIBehaviour::KindContentSizeFitter:
				parts.fitter = static_cast<const UIContentSizeFitter*>(ui);
				break;
			case UIBehaviour::KindRectMask:
				parts.mask = static_cast<const UIRectMask*>(ui);
				break;
			default:
				break;
			}
		}
		return parts;
	}

	template <typename T>
	T* FindEnabled(GameObject& object)
	{
		for (const auto& b : object.GetBehaviours())
		{
			if (T* typed = dynamic_cast<T*>(b.get()); typed != nullptr && typed->enabled) return typed;
		}
		return nullptr;
	}

	template <typename T>
	const T* FindEnabled(const GameObject& object)
	{
		for (const auto& b : object.GetBehaviours())
		{
			if (const T* typed = dynamic_cast<const T*>(b.get()); typed != nullptr && typed->enabled) return typed;
		}
		return nullptr;
	}

	bool IsSelfOrAncestor(const GameObject* ancestor, const GameObject* object)
	{
		for (const GameObject* o = object; o != nullptr; o = o->GetParent())
		{
			if (o == ancestor) return true;
		}
		return false;
	}

	// 유니티 Mathf.SmoothDamp (최대 속도 없음)
	float SmoothDamp(float current, float target, float& velocity, float smoothTime, float dt)
	{
		smoothTime = (std::max)(0.0001f, smoothTime);
		const float omega = 2.0f / smoothTime;
		const float x = omega * dt;
		const float e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
		const float change = current - target;
		const float temp = (velocity + omega * change) * dt;
		velocity = (velocity - omega * temp) * e;
		float output = target + (change + temp) * e;
		if ((target - current > 0.0f) == (output > target))
		{
			output = target;
			velocity = dt > 0.0f ? (output - target) / dt : 0.0f;
		}
		return output;
	}

	// 끝을 넘긴 드래그를 고무줄처럼 줄임 (유니티 ScrollRect.RubberDelta)
	float RubberDelta(float overStretching, float viewSize)
	{
		if (viewSize <= 0.0f) return 0.0f;
		const float sign = overStretching < 0.0f ? -1.0f : 1.0f;
		return (1.0f - (1.0f / ((std::fabs(overStretching) * 0.55f / viewSize) + 1.0f))) * viewSize * sign;
	}
}

// ------------------------------------------------------------------ UIMeshBuilder

void UIMeshBuilder::Begin(UIDrawList& list, uint32_t canvasIndex)
{
	m_list = &list;
	m_canvas = canvasIndex;
}

void UIMeshBuilder::ReportGlyphOverflow()
{
	if (m_warnedOverflow) return;
	m_warnedOverflow = true;
	Log::Warn("UI: 글리프 아틀라스(%u²)를 비우고 다시 구워도 이번 화면의 글자가 다 들어가지 않음 — 넘친 글자는 □ 로 그림", m_atlas != nullptr ? m_atlas->GetSize() : 0);
}

bool UIMeshBuilder::GetTextureSize(const std::string& name, uint32_t& width, uint32_t& height) const
{
	return m_textureSize != nullptr && *m_textureSize && (*m_textureSize)(name, width, height);
}

void UIMeshBuilder::AddQuad(const UIRect& rect, const UIAffine& toCanvas, const XMFLOAT4& uvRect, uint32_t color, const std::string& texture, bool glyph)
{
	if (m_list == nullptr) return;
	UIDrawList& list = *m_list;
	if (m_hasTint)
	{
		// Selectable 색 전환: 바이트마다 곱함 (sRGB 값끼리 — 유니티 ColorTint 와 같음)
		const auto mul = [](uint32_t c, int shift, float t) -> uint32_t
		{
			const float v = static_cast<float>((c >> shift) & 0xFF) * (std::clamp)(t, 0.0f, 1.0f);
			return static_cast<uint32_t>(v + 0.5f) << shift;
		};
		color = mul(color, 0, m_tint.x) | mul(color, 8, m_tint.y) | mul(color, 16, m_tint.z) | mul(color, 24, m_tint.w);
	}
	const uint32_t base = static_cast<uint32_t>(list.vertices.size());
	// 왼쪽 아래, 오른쪽 아래, 오른쪽 위, 왼쪽 위. uv 는 텍스처 관례 (v 아래로 +) 라 위쪽 모서리가 v0.
	const XMFLOAT2 corners[4] = { rect.min, XMFLOAT2(rect.max.x, rect.min.y), rect.max, XMFLOAT2(rect.min.x, rect.max.y) };
	const XMFLOAT2 uvs[4] = { XMFLOAT2(uvRect.x, uvRect.w), XMFLOAT2(uvRect.z, uvRect.w), XMFLOAT2(uvRect.z, uvRect.y), XMFLOAT2(uvRect.x, uvRect.y) };
	for (int i = 0; i < 4; ++i)
	{
		UIVertex v;
		v.position = toCanvas.Apply(corners[i]);
		v.uv = uvs[i];
		v.color = color;
		v.clip = XMFLOAT4(m_clip.min.x, m_clip.min.y, m_clip.max.x, m_clip.max.y);
		v.canvas = m_canvas | (glyph ? kUIVertexGlyphFlag : 0u);
		list.vertices.push_back(v);
	}
	// 컬링을 끄고 그리므로 감김 방향은 상관없음
	const uint32_t quad[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
	const uint32_t start = static_cast<uint32_t>(list.indices.size());
	list.indices.insert(list.indices.end(), quad, quad + 6);

	// 같은 텍스처가 이어지면 앞 명령에 붙임. BreakBatch 뒤(뷰·캔버스가 바뀜)에는 새 명령.
	if (m_mergeAllowed && !list.commands.empty() && list.commands.back().texture == texture && list.commands.back().indexStart + list.commands.back().indexCount == start)
	{
		list.commands.back().indexCount += 6;
	}
	else
	{
		list.commands.push_back(UIDrawCmd{ texture, start, 6 });
		m_mergeAllowed = true;
	}
}

// ------------------------------------------------------------------ 캔버스 찾기

const Canvas* UISystem::FindCanvas(const GameObject& object, const GameObject** canvasObject)
{
	const Canvas* found = nullptr;
	for (const GameObject* o = &object; o != nullptr; o = o->GetParent())
	{
		for (const auto& b : o->GetBehaviours())
		{
			if (const Canvas* canvas = dynamic_cast<const Canvas*>(b.get()); canvas != nullptr && canvas->enabled)
			{
				// 가장 위의 캔버스가 루트 (중첩 캔버스는 지금은 일반 요소로 취급)
				found = canvas;
				if (canvasObject != nullptr)
				{
					*canvasObject = o;
				}
			}
		}
	}
	return found;
}

bool UISystem::IsUnderCanvas(const GameObject& object)
{
	return FindCanvas(object) != nullptr;
}

bool UISystem::IsUnderScreenCanvas(const GameObject& object)
{
	const Canvas* canvas = FindCanvas(object);
	return canvas != nullptr && canvas->renderMode == Canvas::Overlay;
}

// ------------------------------------------------------------------ Build

void UISystem::Build(const Scene& scene, ViewSize mainView, ViewSize previewView)
{
	m_builder.SetTextureSizeProvider(&m_textureSize);
	UIObjectRef::SetSceneContext(&scene);
	// 글리프 아틀라스가 넘치면 키우거나(1024 → 2048) 비운 뒤 한 번 더 만듦. 두 번째에도 넘친 글자는 □.
	// 가장 큰 크기로 비워도 넘치면(포화) 매 프레임 비우고 다시 굽지 않도록 kSaturatedRetryFrames 동안은 있는 글자만 쓰고
	// 나머지는 □ 로 그림 — 그 뒤 한 번 더 비워 봄 (화면의 글자가 바뀌었을 수 있음).
	if (m_atlasSaturated && ++m_saturatedFrames < kSaturatedRetryFrames)
	{
		BuildAll(scene, mainView, previewView, true);
		m_drawList.atlas = &m_atlas.GetImage();
		return;
	}
	if (m_atlasSaturated)
	{
		// 다시 해 봄: 있는 글자로 다 되면 비우지 않고, 새 글자가 들어가지 않으면 아래에서 한 번 비움
		m_atlasSaturated = false;
		m_atlas.ClearOverflow();
	}
	BuildAll(scene, mainView, previewView, false);
	if (m_atlas.Overflowed())
	{
		const uint32_t before = m_atlas.GetSize();
		m_atlas.Grow();
		Log::Info("UI: 글리프 아틀라스 %u² → %u² 로 다시 구움", before, m_atlas.GetSize());
		BuildAll(scene, mainView, previewView, true);
		if (m_atlas.Overflowed())
		{
			m_atlasSaturated = true;
			m_saturatedFrames = 0;
		}
	}
	m_drawList.atlas = m_atlas.GetSize() > 0 ? &m_atlas.GetImage() : nullptr;
}

void UISystem::BuildAll(const Scene& scene, ViewSize mainView, ViewSize previewView, bool finalPass)
{
	m_finalPass = finalPass;
	m_drawList.Clear();
	m_canvases.clear();
	m_elements.clear();
	m_elementOrder.clear();
	m_hitOrder.clear();
	m_selectableOrder.clear();
	m_elementCount = 0;
	m_mainWidth = mainView.enabled ? mainView.width : 0;
	m_mainHeight = mainView.enabled ? mainView.height : 0;
	if (mainView.enabled && mainView.width > 0 && mainView.height > 0)
	{
		m_drawList.views[0].firstCommand = 0;
		BuildView(scene, mainView, 0);
		m_drawList.views[0].commandCount = static_cast<uint32_t>(m_drawList.commands.size());
		m_drawList.views[0].width = mainView.width;
		m_drawList.views[0].height = mainView.height;
	}
	if (previewView.enabled && previewView.width > 0 && previewView.height > 0)
	{
		const uint32_t first = static_cast<uint32_t>(m_drawList.commands.size());
		m_builder.BreakBatch();   // 두 뷰의 명령이 섞이지 않게
		m_drawList.views[1].firstCommand = first;
		BuildView(scene, previewView, 1);
		m_drawList.views[1].commandCount = static_cast<uint32_t>(m_drawList.commands.size()) - first;
		m_drawList.views[1].width = previewView.width;
		m_drawList.views[1].height = previewView.height;
	}
}

void UISystem::BuildView(const Scene& scene, ViewSize view, uint32_t viewIndex)
{
	m_buildScene = &scene;
	m_viewIndex = viewIndex;
	m_driven.clear();
	m_tints.clear();
	m_hidden.clear();

	// 캔버스 루트: Canvas 가 붙었고 그 위로 켜진 Canvas 가 없는 오브젝트. World 를 먼저 (깊이 검사 패스), 같은 모드 안에서는
	// sortOrder, 같으면 씬 순서 (안정 정렬).
	struct Root
	{
		const GameObject* object;
		const Canvas* canvas;
	};
	std::vector<Root> roots;
	for (const auto& owned : scene.GetObjects())
	{
		const GameObject& object = *owned;
		if (object.IsMarkedForDestroy()) continue;
		const GameObject* canvasObject = nullptr;
		const Canvas* canvas = FindCanvas(object, &canvasObject);
		if (canvas != nullptr && canvasObject == &object)
		{
			roots.push_back(Root{ &object, canvas });
		}
	}
	std::stable_sort(roots.begin(), roots.end(), [](const Root& a, const Root& b)
	{
		const bool aw = a.canvas->renderMode == Canvas::World;
		const bool bw = b.canvas->renderMode == Canvas::World;
		if (aw != bw) return aw;
		return a.canvas->sortOrder < b.canvas->sortOrder;
	});

	const float screenW = static_cast<float>(view.width);
	const float screenH = static_cast<float>(view.height);
	UIViewRange& range = m_drawList.views[viewIndex];
	range.worldCommandCount = 0;
	bool worldDone = false;
	for (const Root& root : roots)
	{
		const bool world = root.canvas->renderMode == Canvas::World;
		if (!world && !worldDone)
		{
			worldDone = true;
			range.worldCommandCount = static_cast<uint32_t>(m_drawList.commands.size()) - range.firstCommand;
		}
		if (world && view.camera == nullptr) continue;
		if (m_drawList.canvasMatrices.size() >= kUIMaxCanvases)
		{
			if (!m_warnedCanvasLimit)
			{
				Log::Warn("UI: 캔버스가 %u 개를 넘어 나머지는 그리지 않음 (두 뷰 합계)", kUIMaxCanvases);
				m_warnedCanvasLimit = true;
			}
			break;
		}

		float scale = 1.0f;
		UIRect rootRect;
		XMFLOAT4X4 m;
		if (world)
		{
			// 루트 사각형 = 루트 RectTransform 의 크기·피벗 (부모 없이, 위치는 Transform 이 정함). 캔버스 좌표 → 월드 = 루트의 월드 행렬,
			// → 클립 = 그 카메라의 지터 없는 viewProj (TAA 지터를 따라 흔들리지 않음 — 깊이 경계 1 px 은 3.2 의 한계).
			const RectTransform* rt = root.object->GetBehaviour<RectTransform>();
			const XMFLOAT2 size = rt != nullptr ? rt->sizeDelta : XMFLOAT2(100.0f, 100.0f);
			const XMFLOAT2 pivot = rt != nullptr ? rt->pivot : XMFLOAT2(0.5f, 0.5f);
			rootRect.min = XMFLOAT2(-size.x * pivot.x, -size.y * pivot.y);
			rootRect.max = XMFLOAT2(rootRect.min.x + size.x, rootRect.min.y + size.y);
			scale = (std::max)(0.01f, root.canvas->pixelsPerUnit);
			XMStoreFloat4x4(&m, root.object->GetWorldMatrix() * view.camera->GetViewProjectionMatrix());
		}
		else
		{
			const CanvasScaler* scaler = FindEnabled<CanvasScaler>(*root.object);
			scale = scaler != nullptr ? scaler->ComputeScale(screenW, screenH) : 1.0f;
			// 캔버스 좌표 → 클립: x_clip = x·scale·2/W − 1, y_clip = y·scale·2/H − 1 (캔버스 y 위 + 와 클립 y 위 + 가 같아 뒤집지 않음).
			// 행벡터 관례 행렬 (mul(float4(x, y, 0, 1), M)).
			XMStoreFloat4x4(&m, XMMatrixIdentity());
			m._11 = 2.0f * scale / screenW;
			m._22 = 2.0f * scale / screenH;
			m._41 = -1.0f;
			m._42 = -1.0f;
			rootRect.min = XMFLOAT2(0.0f, 0.0f);
			rootRect.max = XMFLOAT2(screenW / scale, screenH / scale);
		}
		const uint32_t canvasIndex = static_cast<uint32_t>(m_drawList.canvasMatrices.size());
		m_drawList.canvasMatrices.push_back(m);
		if (viewIndex == 0)
		{
			CanvasInfo info;
			info.toClip = m;
			info.world = world;
			info.scale = scale;
			info.viewWidth = view.width;
			info.viewHeight = view.height;
			m_canvases.push_back(info);
		}

		m_viewScale = scale;
		m_measureCache[0].clear();
		m_measureCache[1].clear();
		m_builder.Begin(m_drawList, canvasIndex);
		m_builder.SetContext(scale, !world, &m_fonts, &m_atlas, m_finalPass);
		m_builder.SetClip(NoClip());
		m_builder.SetTint(XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
		m_builder.BreakBatch();   // 캔버스가 바뀌면 새 명령 (순서를 분명히 함)
		Visit(*root.object, rootRect, UIAffine{}, viewIndex, true);
	}
	if (!worldDone)
	{
		range.worldCommandCount = static_cast<uint32_t>(m_drawList.commands.size()) - range.firstCommand;
	}
	m_builder.BreakBatch();   // World 와 Overlay 는 다른 파이프라인 — 다음 뷰와도 섞이지 않게
	m_buildScene = nullptr;
}

void UISystem::Visit(const GameObject& object, const UIRect& parentRect, const UIAffine& parentToCanvas, uint32_t viewIndex, bool isRoot)
{
	if (object.IsMarkedForDestroy()) return;
	const UIParts parts = Gather(object);

	UIRect rect = parentRect;
	UIAffine toCanvas = parentToCanvas;
	bool drivenAxis[2] = {};
	bool drivenAnchors = false;
	if (!isRoot && parts.rect != nullptr)
	{
		const RectTransform& rt = *parts.rect;
		const auto found = m_driven.find(&object);
		const Driven* d = found != m_driven.end() ? &found->second : nullptr;
		XMFLOAT2 pivotP;
		if (d != nullptr && d->anchors)
		{
			rect = rt.ComputeRectWithAnchors(parentRect, d->anchorMin, d->anchorMax, &pivotP);
			drivenAnchors = true;
		}
		else
		{
			rect = rt.ComputeRect(parentRect, &pivotP);
		}
		// 레이아웃 그룹이 정한 축: 부모 사각형 기준 (x 왼쪽에서, y 위에서) 위치와 크기
		for (int axis = 0; axis < 2; ++axis)
		{
			if (d == nullptr || !d->axis[axis]) continue;
			drivenAxis[axis] = true;
			const float size = d->size[axis];
			if (axis == 0)
			{
				rect.min.x = parentRect.min.x + d->pos[0];
				rect.max.x = rect.min.x + size;
			}
			else
			{
				rect.max.y = parentRect.max.y - d->pos[1];
				rect.min.y = rect.max.y - size;
			}
			Component(pivotP, axis) = Component(rect.min, axis) + size * Component(rt.pivot, axis);
		}
		// ContentSizeFitter: 그룹이 정하지 않은 축의 크기를 내용 크기로 (피벗 기준)
		if (parts.fitter != nullptr)
		{
			const int fits[2] = { parts.fitter->horizontalFit, parts.fitter->verticalFit };
			for (int axis = 0; axis < 2; ++axis)
			{
				if (fits[axis] == UIContentSizeFitter::Unconstrained || drivenAxis[axis]) continue;
				const LayoutSize s = Measure(object, axis, rect.Width());
				const float size = fits[axis] == UIContentSizeFitter::MinSize ? s.min : s.preferred;
				const float p = Component(rt.pivot, axis);
				Component(rect.min, axis) = Component(pivotP, axis) - size * p;
				Component(rect.max, axis) = Component(rect.min, axis) + size;
				drivenAxis[axis] = true;
			}
		}
		// ScrollRect 가 옮긴 양
		if (d != nullptr && (d->offset.x != 0.0f || d->offset.y != 0.0f))
		{
			rect.min.x += d->offset.x;
			rect.max.x += d->offset.x;
			rect.min.y += d->offset.y;
			rect.max.y += d->offset.y;
			pivotP.x += d->offset.x;
			pivotP.y += d->offset.y;
		}
		// Transform 의 회전 z (라디안)·스케일 xy 를 피벗 중심으로: local = T(pivot) · R · S · T(−pivot)
		const XMFLOAT3& rotation = object.GetTransform().GetRotation();
		const XMFLOAT3& s = object.GetTransform().GetScale();
		if (rotation.z != 0.0f || s.x != 1.0f || s.y != 1.0f)
		{
			const float c = std::cos(rotation.z);
			const float sn = std::sin(rotation.z);
			UIAffine local;
			local.a = c * s.x;
			local.b = sn * s.x;
			local.c = -sn * s.y;
			local.d = c * s.y;
			local.tx = pivotP.x - (local.a * pivotP.x + local.c * pivotP.y);
			local.ty = pivotP.y - (local.b * pivotP.x + local.d * pivotP.y);
			toCanvas = parentToCanvas.Then(local);
		}
	}

	if (viewIndex == 0)
	{
		ElementInfo info;
		info.canvas = static_cast<uint32_t>(m_canvases.size()) - 1;
		info.rect = rect;
		info.parentRect = parentRect;
		info.parentToCanvas = parentToCanvas;
		info.toCanvas = toCanvas;
		info.clip = m_builder.GetClip();
		info.driven[0] = drivenAxis[0];
		info.driven[1] = drivenAxis[1];
		info.drivenAnchors = drivenAnchors;
		info.root = isRoot;
		const uint64_t id = object.GetId();
		m_elements[id] = info;
		m_elementOrder.push_back(id);
		if (parts.selectable != nullptr)
		{
			m_selectableOrder.push_back(id);
		}
		++m_elementCount;
	}

	// 이 오브젝트의 위젯이 자손에게 정하는 것: 틴트, 숨김, 앵커, 스크롤 위치, 레이아웃
	if (parts.selectable != nullptr)
	{
		ComputeTint(object, *parts.selectable);
	}
	if (parts.toggle != nullptr && !parts.toggle->isOn)
	{
		if (const GameObject* g = UIObjectRef::Resolve(object, parts.toggle->graphic))
		{
			m_hidden[g] = true;
		}
	}
	if (parts.slider != nullptr)
	{
		const UISlider& slider = *parts.slider;
		const int axis = slider.direction >= UISlider::BottomToTop ? 1 : 0;
		const bool reverse = slider.direction == UISlider::RightToLeft || slider.direction == UISlider::TopToBottom;
		const float nv = slider.GetNormalizedValue();
		if (const GameObject* fill = UIObjectRef::Resolve(object, slider.fillRect))
		{
			Driven& d = m_driven[fill];
			d.anchors = true;
			d.anchorMin = XMFLOAT2(0.0f, 0.0f);
			d.anchorMax = XMFLOAT2(1.0f, 1.0f);
			if (reverse)
			{
				Component(d.anchorMin, axis) = 1.0f - nv;
			}
			else
			{
				Component(d.anchorMax, axis) = nv;
			}
		}
		if (const GameObject* handle = UIObjectRef::Resolve(object, slider.handleRect))
		{
			Driven& d = m_driven[handle];
			d.anchors = true;
			d.anchorMin = XMFLOAT2(0.0f, 0.0f);
			d.anchorMax = XMFLOAT2(1.0f, 1.0f);
			Component(d.anchorMin, axis) = Component(d.anchorMax, axis) = reverse ? 1.0f - nv : nv;
		}
	}
	if (parts.scrollbar != nullptr)
	{
		const UIScrollbar& bar = *parts.scrollbar;
		if (const GameObject* handle = UIObjectRef::Resolve(object, bar.handleRect))
		{
			const int axis = bar.direction >= UISlider::BottomToTop ? 1 : 0;
			const bool reverse = bar.direction == UISlider::RightToLeft || bar.direction == UISlider::TopToBottom;
			const float size = (std::clamp)(bar.size, 0.0f, 1.0f);
			const float movement = (std::clamp)(bar.value, 0.0f, 1.0f) * (1.0f - size);
			Driven& d = m_driven[handle];
			d.anchors = true;
			d.anchorMin = XMFLOAT2(0.0f, 0.0f);
			d.anchorMax = XMFLOAT2(1.0f, 1.0f);
			Component(d.anchorMin, axis) = reverse ? 1.0f - movement - size : movement;
			Component(d.anchorMax, axis) = reverse ? 1.0f - movement : movement + size;
		}
	}
	if (parts.scroll != nullptr)
	{
		const auto found = m_scrollState.find(object.GetId());
		if (found != m_scrollState.end())
		{
			if (const GameObject* content = UIObjectRef::Resolve(object, parts.scroll->content))
			{
				m_driven[content].offset = found->second.offset;
				if (viewIndex == 0)
				{
					found->second.builtOffset = found->second.offset;
				}
			}
		}
	}
	if (parts.group != nullptr)
	{
		LayoutChildren(object, rect);
	}

	// 그리기 (raycastTarget 이 하나라도 있으면 히트 목록에)
	const auto tint = m_tints.find(&object);
	m_builder.SetTint(tint != m_tints.end() ? tint->second : XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
	const bool hidden = m_hidden.find(&object) != m_hidden.end();
	bool raycast = false;
	for (int i = 0; i < parts.graphicCount; ++i)
	{
		const UIGraphic* graphic = parts.graphics[i];
		raycast = raycast || graphic->raycastTarget;
		if (!hidden)
		{
			graphic->Populate(m_builder, rect, toCanvas);
		}
	}
	m_builder.SetTint(XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));
	if (raycast && viewIndex == 0)
	{
		m_hitOrder.push_back(object.GetId());
	}

	// 마스크: 자손의 클립 = 지금 클립 ∩ 자기 사각형 (캔버스 축 정렬 상자, padding 만큼 안쪽)
	const UIRect savedClip = m_builder.GetClip();
	if (parts.mask != nullptr)
	{
		UIRect bounds = CanvasBounds(rect, toCanvas);
		bounds.min.x += parts.mask->padding.x;
		bounds.max.x -= parts.mask->padding.x;
		bounds.min.y += parts.mask->padding.y;
		bounds.max.y -= parts.mask->padding.y;
		m_builder.SetClip(Intersect(savedClip, bounds));
	}

	// 자식: Hierarchy 순서 (먼저 그린 것이 아래)
	for (const GameObject* child : object.GetChildren())
	{
		Visit(*child, rect, toCanvas, viewIndex, false);
	}
	m_builder.SetClip(savedClip);
}

void UISystem::ComputeTint(const GameObject& object, const UISelectable& selectable)
{
	if (selectable.transition != UISelectable::TransitionColorTint) return;
	const std::string& path = selectable.targetGraphic.empty() ? std::string(".") : selectable.targetGraphic;
	const GameObject* target = UIObjectRef::Resolve(object, path);
	if (target == nullptr) return;
	XMFLOAT4 color = selectable.interactable ? selectable.normalColor : selectable.disabledColor;
	if (m_playing)
	{
		const auto found = m_selectableState.find(object.GetId());
		if (found != m_selectableState.end() && found->second.initialized)
		{
			color = found->second.color;
		}
	}
	m_tints[target] = color;
}

// ------------------------------------------------------------------ 레이아웃 (유니티 HorizontalOrVerticalLayoutGroup·GridLayoutGroup 와 같은 식)

std::vector<const GameObject*> UISystem::LayoutChildrenOf(const GameObject& object) const
{
	std::vector<const GameObject*> children;
	for (const GameObject* child : object.GetChildren())
	{
		if (child->IsMarkedForDestroy()) continue;
		const RectTransform* rt = FindEnabled<RectTransform>(*child);
		if (rt == nullptr) continue;
		const UILayoutElement* element = FindEnabled<UILayoutElement>(*child);
		if (element != nullptr && element->ignoreLayout) continue;
		children.push_back(child);
	}
	return children;
}

UISystem::LayoutSize UISystem::Measure(const GameObject& object, int axis, float width)
{
	if (axis == 0)
	{
		const auto found = m_measureCache[0].find(&object);
		if (found != m_measureCache[0].end()) return found->second;
	}
	const UIParts parts = Gather(object);
	LayoutSize s;
	if (parts.group != nullptr)
	{
		s = MeasureGroup(object, axis, width);
	}
	for (int i = 0; i < parts.graphicCount; ++i)
	{
		const XMFLOAT2 p = parts.graphics[i]->GetPreferredSize(m_builder, width);
		s.preferred = (std::max)(s.preferred, Component(p, axis));
	}
	if (parts.element != nullptr)
	{
		const UILayoutElement& e = *parts.element;
		const float mn = axis == 0 ? e.minWidth : e.minHeight;
		const float pf = axis == 0 ? e.preferredWidth : e.preferredHeight;
		const float fl = axis == 0 ? e.flexibleWidth : e.flexibleHeight;
		if (mn >= 0.0f) s.min = mn;
		if (pf >= 0.0f) s.preferred = pf;
		if (fl >= 0.0f) s.flexible = fl;
	}
	s.preferred = (std::max)(s.preferred, s.min);
	if (axis == 0)
	{
		m_measureCache[0][&object] = s;
	}
	return s;
}

namespace
{
	struct ChildSlot
	{
		float pos = 0.0f;
		float size = 0.0f;
		bool controlled = true;   // false = 자식 sizeDelta 그대로 (위치만 정함)
	};
}

UISystem::LayoutSize UISystem::MeasureGroup(const GameObject& object, int axis, float width)
{
	LayoutSize total;
	const std::vector<const GameObject*> children = LayoutChildrenOf(object);
	const UIParts parts = Gather(object);
	const UILayoutGroup& group = *parts.group;
	const float padding = axis == 0 ? static_cast<float>(group.paddingLeft + group.paddingRight) : static_cast<float>(group.paddingTop + group.paddingBottom);

	if (const UIGridLayout* grid = dynamic_cast<const UIGridLayout*>(&group))
	{
		const int n = static_cast<int>(children.size());
		int count = 1;
		if (axis == 0)
		{
			int minColumns = 1, prefColumns = 1;
			if (grid->constraint == UIGridLayout::FixedColumnCount)
			{
				minColumns = prefColumns = (std::max)(1, grid->constraintCount);
			}
			else if (grid->constraint == UIGridLayout::FixedRowCount)
			{
				minColumns = prefColumns = (n + (std::max)(1, grid->constraintCount) - 1) / (std::max)(1, grid->constraintCount);
			}
			else
			{
				prefColumns = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(n))));
			}
			total.min = padding + (grid->cellSize.x + grid->spacing.x) * minColumns - grid->spacing.x;
			total.preferred = padding + (grid->cellSize.x + grid->spacing.x) * prefColumns - grid->spacing.x;
			return total;
		}
		if (grid->constraint == UIGridLayout::FixedColumnCount)
		{
			count = (n + (std::max)(1, grid->constraintCount) - 1) / (std::max)(1, grid->constraintCount);
		}
		else if (grid->constraint == UIGridLayout::FixedRowCount)
		{
			count = (std::max)(1, grid->constraintCount);
		}
		else
		{
			const float inner = width - static_cast<float>(group.paddingLeft + group.paddingRight);
			const int columns = (std::max)(1, static_cast<int>(std::floor((inner + grid->spacing.x + 0.001f) / (grid->cellSize.x + grid->spacing.x))));
			count = (n + columns - 1) / columns;
		}
		total.min = total.preferred = padding + (grid->cellSize.y + grid->spacing.y) * count - grid->spacing.y;
		return total;
	}

	const UILinearLayout* linear = dynamic_cast<const UILinearLayout*>(&group);
	if (linear == nullptr) return total;
	const bool alongOther = linear->IsVertical() != (axis == 1);
	const bool control = axis == 0 ? linear->controlChildWidth : linear->controlChildHeight;
	const bool force = axis == 0 ? linear->childForceExpandWidth : linear->childForceExpandHeight;

	// 세로 크기는 자식 너비에 따라 달라짐 (글자 줄바꿈) → 이 그룹의 너비로 자식 너비를 먼저 나눔
	std::vector<float> childWidths(children.size(), 0.0f);
	if (axis == 1)
	{
		const bool controlW = linear->controlChildWidth;
		const bool forceW = linear->childForceExpandWidth;
		const bool alongOtherW = linear->IsVertical();   // 가로축이 교차축인가
		std::vector<LayoutSize> ws(children.size());
		float tMin = static_cast<float>(group.paddingLeft + group.paddingRight), tPref = tMin, tFlex = 0.0f;
		for (size_t i = 0; i < children.size(); ++i)
		{
			LayoutSize c;
			if (!controlW)
			{
				c.min = c.preferred = children[i]->GetBehaviour<RectTransform>()->sizeDelta.x;
			}
			else
			{
				c = Measure(*children[i], 0, 0.0f);
			}
			if (forceW)
			{
				c.flexible = (std::max)(c.flexible, 1.0f);
			}
			ws[i] = c;
			if (!alongOtherW)
			{
				tMin += c.min + linear->spacing;
				tPref += c.preferred + linear->spacing;
				tFlex += c.flexible;
			}
		}
		const float inner = width - static_cast<float>(group.paddingLeft + group.paddingRight);
		if (alongOtherW)
		{
			for (size_t i = 0; i < children.size(); ++i)
			{
				childWidths[i] = (std::clamp)(inner, ws[i].min, ws[i].flexible > 0.0f ? width : ws[i].preferred);
			}
		}
		else
		{
			if (!children.empty())
			{
				tMin -= linear->spacing;
				tPref -= linear->spacing;
			}
			tPref = (std::max)(tMin, tPref);
			const float surplus = width - tPref;
			const float mult = surplus > 0.0f && tFlex > 0.0f ? surplus / tFlex : 0.0f;
			const float t = tMin != tPref ? (std::clamp)((width - tMin) / (tPref - tMin), 0.0f, 1.0f) : 0.0f;
			for (size_t i = 0; i < children.size(); ++i)
			{
				childWidths[i] = ws[i].min + (ws[i].preferred - ws[i].min) * t + ws[i].flexible * mult;
			}
		}
	}

	total.min = padding;
	total.preferred = padding;
	for (size_t i = 0; i < children.size(); ++i)
	{
		LayoutSize c;
		if (!control)
		{
			const XMFLOAT2& sd = children[i]->GetBehaviour<RectTransform>()->sizeDelta;
			c.min = c.preferred = Component(sd, axis);
		}
		else
		{
			c = Measure(*children[i], axis, childWidths[i]);
		}
		if (force)
		{
			c.flexible = (std::max)(c.flexible, 1.0f);
		}
		if (alongOther)
		{
			total.min = (std::max)(total.min, c.min + padding);
			total.preferred = (std::max)(total.preferred, c.preferred + padding);
			total.flexible = (std::max)(total.flexible, c.flexible);
		}
		else
		{
			total.min += c.min + linear->spacing;
			total.preferred += c.preferred + linear->spacing;
			total.flexible += c.flexible;
		}
	}
	if (!alongOther && !children.empty())
	{
		total.min -= linear->spacing;
		total.preferred -= linear->spacing;
	}
	total.preferred = (std::max)(total.min, total.preferred);
	return total;
}

void UISystem::LayoutChildren(const GameObject& object, const UIRect& rect)
{
	const UIParts parts = Gather(object);
	const UILayoutGroup& group = *parts.group;
	const std::vector<const GameObject*> children = LayoutChildrenOf(object);
	if (children.empty()) return;
	const float padStart[2] = { static_cast<float>(group.paddingLeft), static_cast<float>(group.paddingTop) };
	const float padSum[2] = { static_cast<float>(group.paddingLeft + group.paddingRight), static_cast<float>(group.paddingTop + group.paddingBottom) };
	const float sizes[2] = { rect.Width(), rect.Height() };
	const float alignOnAxis[2] = { static_cast<float>(group.childAlignment % 3) * 0.5f, static_cast<float>(group.childAlignment / 3) * 0.5f };
	// 정렬 기준 시작 위치 (유니티 LayoutGroup.GetStartOffset)
	const auto startOffset = [&](int axis, float requiredWithoutPadding)
	{
		const float required = requiredWithoutPadding + padSum[axis];
		return padStart[axis] + (sizes[axis] - required) * alignOnAxis[axis];
	};
	const auto set = [&](const GameObject* child, int axis, float pos, float size)
	{
		Driven& d = m_driven[child];
		d.axis[axis] = true;
		d.pos[axis] = pos;
		d.size[axis] = size;
	};

	if (const UIGridLayout* grid = dynamic_cast<const UIGridLayout*>(&group))
	{
		const int n = static_cast<int>(children.size());
		const XMFLOAT2 cell = grid->cellSize;
		const XMFLOAT2 spacing = grid->spacing;
		int cellCountX = 1, cellCountY = 1;
		if (grid->constraint == UIGridLayout::FixedColumnCount)
		{
			cellCountX = (std::max)(1, grid->constraintCount);
			cellCountY = (n + cellCountX - 1) / cellCountX;
		}
		else if (grid->constraint == UIGridLayout::FixedRowCount)
		{
			cellCountY = (std::max)(1, grid->constraintCount);
			cellCountX = (n + cellCountY - 1) / cellCountY;
		}
		else
		{
			cellCountX = cell.x + spacing.x <= 0.0f ? INT_MAX : (std::max)(1, static_cast<int>(std::floor((sizes[0] - padSum[0] + spacing.x + 0.001f) / (cell.x + spacing.x))));
			cellCountY = cell.y + spacing.y <= 0.0f ? INT_MAX : (std::max)(1, static_cast<int>(std::floor((sizes[1] - padSum[1] + spacing.y + 0.001f) / (cell.y + spacing.y))));
		}
		const int cornerX = grid->startCorner % 2;
		const int cornerY = grid->startCorner / 2;
		int cellsPerMainAxis, actualX, actualY;
		if (grid->startAxis == 0)
		{
			cellsPerMainAxis = cellCountX;
			actualX = (std::clamp)(cellCountX, 1, n);
			actualY = (std::clamp)(cellCountY, 1, (n + cellsPerMainAxis - 1) / cellsPerMainAxis);
		}
		else
		{
			cellsPerMainAxis = cellCountY;
			actualY = (std::clamp)(cellCountY, 1, n);
			actualX = (std::clamp)(cellCountX, 1, (n + cellsPerMainAxis - 1) / cellsPerMainAxis);
		}
		const float requiredX = actualX * cell.x + (actualX - 1) * spacing.x;
		const float requiredY = actualY * cell.y + (actualY - 1) * spacing.y;
		const float sx = startOffset(0, requiredX);
		const float sy = startOffset(1, requiredY);
		for (int i = 0; i < n; ++i)
		{
			int px, py;
			if (grid->startAxis == 0)
			{
				px = i % cellsPerMainAxis;
				py = i / cellsPerMainAxis;
			}
			else
			{
				px = i / cellsPerMainAxis;
				py = i % cellsPerMainAxis;
			}
			if (cornerX == 1) px = actualX - 1 - px;
			if (cornerY == 1) py = actualY - 1 - py;
			set(children[i], 0, sx + (cell.x + spacing.x) * px, cell.x);
			set(children[i], 1, sy + (cell.y + spacing.y) * py, cell.y);
		}
		return;
	}

	const UILinearLayout* linear = dynamic_cast<const UILinearLayout*>(&group);
	if (linear == nullptr) return;
	std::vector<float> childWidths(children.size(), 0.0f);
	for (int axis = 0; axis < 2; ++axis)
	{
		const bool alongOther = linear->IsVertical() != (axis == 1);
		const bool control = axis == 0 ? linear->controlChildWidth : linear->controlChildHeight;
		const bool force = axis == 0 ? linear->childForceExpandWidth : linear->childForceExpandHeight;
		const float size = sizes[axis];
		std::vector<LayoutSize> cs(children.size());
		std::vector<float> natural(children.size());
		for (size_t i = 0; i < children.size(); ++i)
		{
			natural[i] = Component(children[i]->GetBehaviour<RectTransform>()->sizeDelta, axis);
			LayoutSize c;
			if (!control)
			{
				c.min = c.preferred = natural[i];
			}
			else
			{
				c = Measure(*children[i], axis, childWidths[i]);
			}
			if (force)
			{
				c.flexible = (std::max)(c.flexible, 1.0f);
			}
			cs[i] = c;
		}
		if (alongOther)
		{
			const float inner = size - padSum[axis];
			for (size_t i = 0; i < children.size(); ++i)
			{
				const float required = (std::clamp)(inner, cs[i].min, cs[i].flexible > 0.0f ? size : cs[i].preferred);
				const float start = startOffset(axis, required);
				if (control)
				{
					set(children[i], axis, start, required);
				}
				else
				{
					set(children[i], axis, start + (required - natural[i]) * alignOnAxis[axis], natural[i]);
				}
				if (axis == 0)
				{
					childWidths[i] = control ? required : natural[i];
				}
			}
			continue;
		}
		float tMin = padSum[axis], tPref = padSum[axis], tFlex = 0.0f;
		for (const LayoutSize& c : cs)
		{
			tMin += c.min + linear->spacing;
			tPref += c.preferred + linear->spacing;
			tFlex += c.flexible;
		}
		tMin -= linear->spacing;
		tPref -= linear->spacing;
		tPref = (std::max)(tMin, tPref);
		float pos = padStart[axis];
		float mult = 0.0f;
		const float surplus = size - tPref;
		if (surplus > 0.0f)
		{
			if (tFlex == 0.0f)
			{
				pos = startOffset(axis, tPref - padSum[axis]);
			}
			else
			{
				mult = surplus / tFlex;
			}
		}
		const float t = tMin != tPref ? (std::clamp)((size - tMin) / (tPref - tMin), 0.0f, 1.0f) : 0.0f;
		for (size_t i = 0; i < children.size(); ++i)
		{
			const float childSize = cs[i].min + (cs[i].preferred - cs[i].min) * t + cs[i].flexible * mult;
			if (control)
			{
				set(children[i], axis, pos, childSize);
			}
			else
			{
				set(children[i], axis, pos + (childSize - natural[i]) * alignOnAxis[axis], natural[i]);
			}
			if (axis == 0)
			{
				childWidths[i] = control ? childSize : natural[i];
			}
			pos += childSize + linear->spacing;
		}
	}
}

// ------------------------------------------------------------------ 좌표

bool UISystem::PixelToCanvas(const CanvasInfo& canvas, float px, float py, XMFLOAT2& out, float* w) const
{
	if (canvas.viewWidth == 0 || canvas.viewHeight == 0) return false;
	// 출력 픽셀 → NDC, 그리고 [cx cy 0 1]·M 의 x/w = nx, y/w = ny 를 cx, cy 에 대해 풂 (World 는 캔버스 평면과 시선의 교점)
	const float nx = 2.0f * px / static_cast<float>(canvas.viewWidth) - 1.0f;
	const float ny = 1.0f - 2.0f * py / static_cast<float>(canvas.viewHeight);
	const XMFLOAT4X4& m = canvas.toClip;
	const float a11 = m._11 - nx * m._14, a12 = m._21 - nx * m._24, b1 = nx * m._44 - m._41;
	const float a21 = m._12 - ny * m._14, a22 = m._22 - ny * m._24, b2 = ny * m._44 - m._42;
	const float det = a11 * a22 - a12 * a21;
	if (std::fabs(det) < 1e-12f) return false;
	out.x = (b1 * a22 - a12 * b2) / det;
	out.y = (a11 * b2 - b1 * a21) / det;
	const float clipW = out.x * m._14 + out.y * m._24 + m._44;
	if (clipW <= 1e-6f) return false;   // 카메라 뒤
	if (w != nullptr)
	{
		*w = clipW;
	}
	return true;
}

bool UISystem::ElementContains(const ElementInfo& e, float px, float py, float* w) const
{
	if (e.canvas >= m_canvases.size()) return false;
	XMFLOAT2 c;
	if (!PixelToCanvas(m_canvases[e.canvas], px, py, c, w)) return false;
	if (c.x < e.clip.min.x || c.y < e.clip.min.y || c.x > e.clip.max.x || c.y > e.clip.max.y) return false;
	UIAffine inv;
	if (!e.toCanvas.Invert(inv)) return false;
	const XMFLOAT2 local = inv.Apply(c);
	return local.x >= e.rect.min.x && local.x <= e.rect.max.x && local.y >= e.rect.min.y && local.y <= e.rect.max.y;
}

uint64_t UISystem::HitTest(float px, float py) const
{
	// 위에 그린 것부터. Overlay 는 World 뒤에 그리므로 역순이면 Overlay 가 먼저 나옴 → 맞으면 바로 끝.
	// World 끼리는 가까운 캔버스(투영 w 가 작은 것). 장면 물체에 가렸는지는 보지 않음 (유니티 기본과 같음).
	uint64_t best = 0;
	float bestW = 0.0f;
	for (auto it = m_hitOrder.rbegin(); it != m_hitOrder.rend(); ++it)
	{
		const auto found = m_elements.find(*it);
		if (found == m_elements.end()) continue;
		const ElementInfo& e = found->second;
		float w = 0.0f;
		if (!ElementContains(e, px, py, &w)) continue;
		if (!m_canvases[e.canvas].world) return *it;
		if (best == 0 || w < bestW - 1e-5f)
		{
			best = *it;
			bestW = w;
		}
	}
	return best;
}

bool UISystem::PixelToLocal(uint64_t id, float px, float py, XMFLOAT2& local) const
{
	const auto found = m_elements.find(id);
	if (found == m_elements.end()) return false;
	const ElementInfo& e = found->second;
	XMFLOAT2 c;
	if (e.canvas >= m_canvases.size() || !PixelToCanvas(m_canvases[e.canvas], px, py, c)) return false;
	UIAffine inv;
	if (!e.toCanvas.Invert(inv)) return false;
	local = inv.Apply(c);
	return true;
}

bool UISystem::PixelToParent(uint64_t id, float px, float py, XMFLOAT2& out) const
{
	const auto found = m_elements.find(id);
	if (found == m_elements.end()) return false;
	const ElementInfo& e = found->second;
	XMFLOAT2 c;
	if (e.canvas >= m_canvases.size() || !PixelToCanvas(m_canvases[e.canvas], px, py, c)) return false;
	UIAffine inv;
	if (!e.parentToCanvas.Invert(inv)) return false;
	out = inv.Apply(c);
	return true;
}

bool UISystem::GetScreenQuad(const GameObject* object, XMFLOAT2 out[4]) const
{
	if (object == nullptr) return false;
	const auto found = m_elements.find(object->GetId());
	if (found == m_elements.end()) return false;
	const ElementInfo& e = found->second;
	if (e.canvas >= m_canvases.size()) return false;
	const CanvasInfo& canvas = m_canvases[e.canvas];
	const XMFLOAT2 corners[4] = { e.rect.min, XMFLOAT2(e.rect.max.x, e.rect.min.y), e.rect.max, XMFLOAT2(e.rect.min.x, e.rect.max.y) };
	const XMMATRIX m = XMLoadFloat4x4(&canvas.toClip);
	for (int i = 0; i < 4; ++i)
	{
		const XMFLOAT2 c = e.toCanvas.Apply(corners[i]);
		XMFLOAT4 clip;
		XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(c.x, c.y, 0.0f, 1.0f), m));
		if (clip.w <= 1e-6f) return false;
		out[i] = XMFLOAT2((clip.x / clip.w + 1.0f) * 0.5f * canvas.viewWidth, (1.0f - clip.y / clip.w) * 0.5f * canvas.viewHeight);
	}
	return true;
}

bool UISystem::GetElementGeometry(const GameObject* object, ElementGeometry& out) const
{
	if (object == nullptr) return false;
	const auto found = m_elements.find(object->GetId());
	if (found == m_elements.end()) return false;
	const ElementInfo& e = found->second;
	if (e.canvas >= m_canvases.size() || m_canvases[e.canvas].world) return false;
	out.rect = e.rect;
	out.parentRect = e.parentRect;
	out.parentToCanvas = e.parentToCanvas;
	out.toCanvas = e.toCanvas;
	out.scale = m_canvases[e.canvas].scale;
	out.viewHeight = m_canvases[e.canvas].viewHeight;
	out.driven[0] = e.driven[0];
	out.driven[1] = e.driven[1];
	out.drivenAnchors = e.drivenAnchors;
	return true;
}

uint64_t UISystem::PickElement(float px, float py, uint64_t after) const
{
	// 캔버스 루트는 고르지 않음 (화면 전체라 다른 것을 가림)
	std::vector<uint64_t> hits;
	for (auto it = m_elementOrder.rbegin(); it != m_elementOrder.rend(); ++it)
	{
		const auto found = m_elements.find(*it);
		if (found == m_elements.end() || found->second.root) continue;
		if (ElementContains(found->second, px, py, nullptr))
		{
			hits.push_back(*it);
		}
	}
	if (hits.empty()) return 0;
	if (after != 0)
	{
		const auto it = std::find(hits.begin(), hits.end(), after);
		if (it != hits.end())
		{
			return (it + 1) != hits.end() ? *(it + 1) : hits.front();
		}
	}
	return hits.front();
}

// ------------------------------------------------------------------ 재생

void UISystem::BeginPlay()
{
	m_selectableState.clear();
	m_scrollState.clear();
	m_hoverId = m_pressedId = m_dragId = m_selectedId = 0;
	m_dragStarted = false;
	m_selectionVisible = false;
	m_pointerOverUI = false;
	m_hasPointer = false;
	m_lastPointerX = m_lastPointerY = -1.0f;
	m_navX = m_navY = 0;
	m_navRepeating = false;
	m_playing = true;
}

void UISystem::EndPlay(Scene& scene)
{
	BeginPlay();
	m_playing = false;
	// 코드 리스너는 스크립트 DLL 의 함수를 가리킴 — 재생이 끝나면 버림 (저장되는 연결은 그대로)
	for (const auto& owned : scene.GetObjects())
	{
		for (const auto& b : owned->GetBehaviours())
		{
			if (auto* button = dynamic_cast<UIButton*>(b.get())) button->onClick.RemoveAllListeners();
			else if (auto* toggle = dynamic_cast<UIToggle*>(b.get())) toggle->onValueChanged.RemoveAllListeners();
			else if (auto* slider = dynamic_cast<UISlider*>(b.get())) slider->onValueChanged.RemoveAllListeners();
			else if (auto* bar = dynamic_cast<UIScrollbar*>(b.get())) bar->onValueChanged.RemoveAllListeners();
			else if (auto* es = dynamic_cast<UIEventSystem*>(b.get())) es->onCancel.RemoveAllListeners();
		}
	}
}

void UISystem::SetPointerViewport(float x, float y, float width, float height, bool enabled)
{
	m_viewportX = x;
	m_viewportY = y;
	m_viewportW = width;
	m_viewportH = height;
	m_viewportSet = true;
	m_viewportEnabled = enabled;
}

void UISystem::SetSelected(const GameObject* object)
{
	m_selectedId = object != nullptr ? object->GetId() : 0;
}

void UISystem::Update(Scene& scene, Input& input, float dt)
{
	UIObjectRef::SetSceneContext(&scene);
	UpdatePointer(scene, input, dt);
	UpdateNavigation(scene, input, dt);
	UpdateScrollRects(scene, dt);
	UpdateColors(scene, dt);
}

void UISystem::UpdatePointer(Scene& scene, Input& input, float dt)
{
	(void)dt;
	// 포인터 → 출력 픽셀. 에디터는 씬 이미지 사각형 (지난 OnGUI 에서 받은 것), 게임은 창 전체.
	const float vw = m_viewportSet ? m_viewportW : static_cast<float>(m_mainWidth);
	const float vh = m_viewportSet ? m_viewportH : static_cast<float>(m_mainHeight);
	const float vx = m_viewportSet ? m_viewportX : 0.0f;
	const float vy = m_viewportSet ? m_viewportY : 0.0f;
	float px = -1.0f, py = -1.0f;
	bool inside = false;
	if (m_viewportEnabled && vw > 0.0f && vh > 0.0f && m_mainWidth > 0 && m_mainHeight > 0)
	{
		px = (static_cast<float>(input.GetMouseX()) - vx) * static_cast<float>(m_mainWidth) / vw;
		py = (static_cast<float>(input.GetMouseY()) - vy) * static_cast<float>(m_mainHeight) / vh;
		inside = px >= 0.0f && py >= 0.0f && px < static_cast<float>(m_mainWidth) && py < static_cast<float>(m_mainHeight);
	}
	m_hasPointer = inside;
	const bool moved = m_lastPointerX >= 0.0f && (std::fabs(px - m_lastPointerX) > 0.5f || std::fabs(py - m_lastPointerY) > 0.5f);
	m_lastPointerX = px;
	m_lastPointerY = py;
	if (moved && inside)
	{
		m_selectionVisible = false;   // 마우스를 움직이면 포인터 모드 (선택 표시 숨김)
	}

	const uint64_t hitId = inside ? HitTest(px, py) : 0;
	m_pointerOverUI = hitId != 0;

	// 맞은 Graphic 에서 위로: 처음 만나는 Selectable(누르기·클릭), 처음 만나는 드래그 대상(Slider·Scrollbar·ScrollRect)
	uint64_t hoverSel = 0;
	bool hoverInteractable = false;
	uint64_t dragTarget = 0;
	for (GameObject* o = hitId != 0 ? scene.FindById(hitId) : nullptr; o != nullptr; o = o->GetParent())
	{
		UISelectable* sel = FindEnabled<UISelectable>(*o);
		if (hoverSel == 0 && sel != nullptr)
		{
			hoverSel = o->GetId();
			hoverInteractable = sel->interactable;
		}
		if (dragTarget == 0)
		{
			if ((dynamic_cast<UISlider*>(sel) != nullptr || dynamic_cast<UIScrollbar*>(sel) != nullptr) && sel->interactable)
			{
				dragTarget = o->GetId();
			}
			else if (FindEnabled<UIScrollRect>(*o) != nullptr)
			{
				dragTarget = o->GetId();
			}
		}
		if (hoverSel != 0 && dragTarget != 0) break;
	}
	m_hoverId = hoverInteractable ? hoverSel : 0;

	if (input.IsMousePressed(MouseButton::Left) && inside)
	{
		m_pressedId = hoverInteractable ? hoverSel : 0;
		m_dragId = dragTarget;
		m_dragStarted = false;
		m_pressX = px;
		m_pressY = py;
		// 누른 Selectable 을 선택 (다른 곳을 누르면 선택 해제 — 유니티 StandaloneInputModule 과 같음)
		m_selectedId = m_pressedId;
		if (GameObject* drag = m_dragId != 0 ? scene.FindById(m_dragId) : nullptr)
		{
			if (FindEnabled<UISlider>(*drag) != nullptr)
			{
				DragSlider(scene, *drag, px, py);
			}
			else if (FindEnabled<UIScrollbar>(*drag) != nullptr)
			{
				DragScrollbar(scene, *drag, px, py, true);
			}
		}
	}
	else if (input.IsMouseDown(MouseButton::Left) && m_dragId != 0)
	{
		GameObject* drag = scene.FindById(m_dragId);
		if (drag == nullptr)
		{
			m_dragId = 0;
		}
		else if (FindEnabled<UISlider>(*drag) != nullptr)
		{
			if (moved)
			{
				DragSlider(scene, *drag, px, py);
			}
		}
		else if (FindEnabled<UIScrollbar>(*drag) != nullptr)
		{
			if (moved)
			{
				DragScrollbar(scene, *drag, px, py, false);
			}
		}
		else if (const UIScrollRect* scroll = FindEnabled<UIScrollRect>(*drag))
		{
			GameObject* content = UIObjectRef::Resolve(*drag, scroll->content);
			ScrollRuntime& state = m_scrollState[m_dragId];
			if (!m_dragStarted && content != nullptr && std::hypot(px - m_pressX, py - m_pressY) >= kDragThreshold)
			{
				m_dragStarted = true;
				m_pressedId = 0;   // 드래그가 시작되면 누른 버튼은 클릭으로 치지 않음
				state.dragging = true;
				state.dragStartOffset = state.offset;
				PixelToParent(content->GetId(), m_pressX, m_pressY, state.dragStartPointer);
			}
			if (m_dragStarted && content != nullptr)
			{
				XMFLOAT2 p;
				if (PixelToParent(content->GetId(), px, py, p))
				{
					// content 부모 공간의 포인터 이동 = 옮긴 양. 지난 Build 의 content 위치에는 그때의 offset 이 들어 있으므로
					// 부모 공간 좌표는 offset 과 무관함 (content 자신이 아니라 부모 기준으로 잼).
					state.offset = XMFLOAT2(state.dragStartOffset.x + (scroll->horizontal ? p.x - state.dragStartPointer.x : 0.0f),
						state.dragStartOffset.y + (scroll->vertical ? p.y - state.dragStartPointer.y : 0.0f));
					// 끝을 넘으면: Clamped 는 막고, Elastic 은 고무줄 (유니티 ScrollRect.OnDrag)
					XMFLOAT2 over, viewSize;
					if (scroll->movementType != UIScrollRect::Unrestricted && ScrollOverflow(scene, *drag, state, over, &viewSize))
					{
						XMFLOAT2 fix = over;
						if (scroll->movementType == UIScrollRect::Elastic)
						{
							fix.x = over.x != 0.0f ? over.x - RubberDelta(over.x, viewSize.x) : 0.0f;
							fix.y = over.y != 0.0f ? over.y - RubberDelta(over.y, viewSize.y) : 0.0f;
						}
						const XMFLOAT2 local = CanvasToContentParent(*content, fix);
						state.offset.x += local.x;
						state.offset.y += local.y;
					}
				}
			}
		}
	}

	if (input.IsMouseReleased(MouseButton::Left))
	{
		if (m_pressedId != 0 && m_pressedId == hoverSel && !m_dragStarted)
		{
			if (GameObject* pressed = scene.FindById(m_pressedId))
			{
				Click(scene, *pressed);
			}
		}
		if (m_dragId != 0)
		{
			const auto found = m_scrollState.find(m_dragId);
			if (found != m_scrollState.end())
			{
				found->second.dragging = false;
			}
		}
		m_pressedId = 0;
		m_dragId = 0;
		m_dragStarted = false;
	}

	// 휠: 포인터 아래에서 위로 처음 만나는 ScrollRect
	const float wheel = input.GetWheelDelta();
	if (wheel != 0.0f && hitId != 0)
	{
		for (GameObject* o = scene.FindById(hitId); o != nullptr; o = o->GetParent())
		{
			const UIScrollRect* scroll = FindEnabled<UIScrollRect>(*o);
			if (scroll == nullptr) continue;
			GameObject* content = UIObjectRef::Resolve(*o, scroll->content);
			if (content == nullptr) break;
			// 유니티 ScrollRect.OnScroll: 휠 위(+) = 내용이 아래로 (앞쪽이 보임). 한 방향만 스크롤하면 휠을 그 축에
			XMFLOAT2 delta(0.0f, -wheel);
			if (scroll->horizontal && !scroll->vertical)
			{
				delta = XMFLOAT2(-wheel, 0.0f);
			}
			delta.x *= scroll->horizontal ? scroll->scrollSensitivity : 0.0f;
			delta.y *= scroll->vertical ? scroll->scrollSensitivity : 0.0f;   // offset 은 content 부모 공간 (y 위 +): 휠 아래(−) → +y (내용이 위로)
			ScrollRuntime& state = m_scrollState[o->GetId()];
			state.offset.x += delta.x;
			state.offset.y += delta.y;
			state.velocity = XMFLOAT2(0.0f, 0.0f);
			if (scroll->movementType == UIScrollRect::Clamped)
			{
				ScrollBy(scene, *o, XMFLOAT2(0.0f, 0.0f), true);
			}
			break;
		}
	}
}

void UISystem::Click(Scene& scene, GameObject& object)
{
	if (UIButton* button = FindEnabled<UIButton>(object); button != nullptr && button->interactable)
	{
		button->onClick.Invoke(object);
		return;
	}
	if (UIToggle* toggle = FindEnabled<UIToggle>(object); toggle != nullptr && toggle->interactable)
	{
		SetToggle(scene, object, !toggle->isOn);
	}
}

void UISystem::Submit(Scene& scene, GameObject& object)
{
	Click(scene, object);
}

void UISystem::SetToggle(Scene& scene, GameObject& toggleObject, bool on)
{
	UIToggle* toggle = FindEnabled<UIToggle>(toggleObject);
	if (toggle == nullptr) return;
	GameObject* groupObject = UIObjectRef::Resolve(toggleObject, toggle->group);
	const UIToggleGroup* group = groupObject != nullptr ? FindEnabled<UIToggleGroup>(*groupObject) : nullptr;
	if (group == nullptr)
	{
		toggle->SetIsOn(on);
		return;
	}
	if (!on)
	{
		// 그룹: 켜진 것을 다시 눌러 끄는 것은 allowSwitchOff 일 때만
		if (group->allowSwitchOff)
		{
			toggle->SetIsOn(false);
		}
		return;
	}
	// 같은 그룹의 다른 토글을 끈 뒤 이것을 켬 (id 로 다시 찾음 — 리스너가 오브젝트를 바꿀 수 있음)
	const uint64_t selfId = toggleObject.GetId();
	std::vector<uint64_t> others;
	for (const auto& owned : scene.GetObjects())
	{
		if (owned->GetId() == selfId || owned->IsMarkedForDestroy()) continue;
		UIToggle* other = FindEnabled<UIToggle>(*owned);
		if (other != nullptr && other->isOn && UIObjectRef::Resolve(*owned, other->group) == groupObject)
		{
			others.push_back(owned->GetId());
		}
	}
	for (uint64_t id : others)
	{
		if (GameObject* o = scene.FindById(id))
		{
			if (UIToggle* other = FindEnabled<UIToggle>(*o))
			{
				other->SetIsOn(false);
			}
		}
	}
	if (GameObject* self = scene.FindById(selfId))
	{
		if (UIToggle* t = FindEnabled<UIToggle>(*self))
		{
			t->SetIsOn(true);
		}
	}
}

void UISystem::DragSlider(Scene& scene, GameObject& object, float px, float py)
{
	(void)scene;
	UISlider* slider = FindEnabled<UISlider>(object);
	if (slider == nullptr || !slider->interactable) return;
	// 누른 위치는 손잡이의 부모 사각형 기준 (없으면 fill 의 부모, 그것도 없으면 자기)
	const GameObject* area = nullptr;
	if (const GameObject* handle = UIObjectRef::Resolve(object, slider->handleRect))
	{
		area = handle->GetParent();
	}
	if (area == nullptr)
	{
		if (const GameObject* fill = UIObjectRef::Resolve(object, slider->fillRect))
		{
			area = fill->GetParent();
		}
	}
	if (area == nullptr)
	{
		area = &object;
	}
	const auto found = m_elements.find(area->GetId());
	XMFLOAT2 local;
	if (found == m_elements.end() || !PixelToLocal(area->GetId(), px, py, local)) return;
	const UIRect& r = found->second.rect;
	const int axis = slider->direction >= UISlider::BottomToTop ? 1 : 0;
	const bool reverse = slider->direction == UISlider::RightToLeft || slider->direction == UISlider::TopToBottom;
	const float length = axis == 0 ? r.Width() : r.Height();
	if (length <= 0.0f) return;
	float t = (std::clamp)((Component(local, axis) - Component(r.min, axis)) / length, 0.0f, 1.0f);
	if (reverse)
	{
		t = 1.0f - t;
	}
	slider->SetValue(slider->minValue + (slider->maxValue - slider->minValue) * t);
}

void UISystem::DragScrollbar(Scene& scene, GameObject& object, float px, float py, bool begin)
{
	(void)scene;
	UIScrollbar* bar = FindEnabled<UIScrollbar>(object);
	if (bar == nullptr || !bar->interactable) return;
	const GameObject* handle = UIObjectRef::Resolve(object, bar->handleRect);
	const GameObject* area = handle != nullptr ? handle->GetParent() : nullptr;
	if (handle == nullptr || area == nullptr) return;
	const auto areaInfo = m_elements.find(area->GetId());
	const auto handleInfo = m_elements.find(handle->GetId());
	XMFLOAT2 local;
	if (areaInfo == m_elements.end() || handleInfo == m_elements.end() || !PixelToLocal(area->GetId(), px, py, local)) return;
	const UIRect& r = areaInfo->second.rect;
	const UIRect& h = handleInfo->second.rect;   // area 의 rect 공간 (= local 과 같은 공간)
	const int axis = bar->direction >= UISlider::BottomToTop ? 1 : 0;
	const bool reverse = bar->direction == UISlider::RightToLeft || bar->direction == UISlider::TopToBottom;
	const float length = axis == 0 ? r.Width() : r.Height();
	const float handleLength = axis == 0 ? h.Width() : h.Height();
	const float p = Component(local, axis);
	if (begin)
	{
		// 손잡이 안을 잡으면 그 자리 그대로, 밖이면 손잡이 가운데로 옮김
		const bool onHandle = p >= Component(h.min, axis) && p <= Component(h.max, axis);
		m_scrollbarGrab = onHandle ? p - Component(h.min, axis) : handleLength * 0.5f;
	}
	const float remaining = length - handleLength;
	if (remaining <= 0.0f) return;
	float v = (std::clamp)((p - m_scrollbarGrab - Component(r.min, axis)) / remaining, 0.0f, 1.0f);
	if (reverse)
	{
		v = 1.0f - v;
	}
	bar->SetValue(v);
}

// ------------------------------------------------------------------ 스크롤

bool UISystem::GetScrollBounds(Scene& scene, GameObject& scrollObject, UIRect& view, UIRect& content) const
{
	(void)scene;
	const UIScrollRect* scroll = FindEnabled<UIScrollRect>(scrollObject);
	if (scroll == nullptr) return false;
	const GameObject* viewport = scroll->viewport.empty() ? &scrollObject : UIObjectRef::Resolve(scrollObject, scroll->viewport);
	const GameObject* contentObject = UIObjectRef::Resolve(scrollObject, scroll->content);
	if (viewport == nullptr || contentObject == nullptr) return false;
	const auto v = m_elements.find(viewport->GetId());
	const auto c = m_elements.find(contentObject->GetId());
	if (v == m_elements.end() || c == m_elements.end()) return false;
	view = CanvasBounds(v->second.rect, v->second.toCanvas);
	content = CanvasBounds(c->second.rect, c->second.toCanvas);
	// 내용이 뷰보다 작으면 뷰 크기로 늘림 (content 피벗 기준 — 유니티 ScrollRect.UpdateBounds)
	const RectTransform* crt = contentObject->GetBehaviour<RectTransform>();
	const XMFLOAT2 pivot = crt != nullptr ? crt->pivot : XMFLOAT2(0.5f, 0.5f);
	for (int axis = 0; axis < 2; ++axis)
	{
		const float viewSize = Component(view.max, axis) - Component(view.min, axis);
		const float contentSize = Component(content.max, axis) - Component(content.min, axis);
		const float excess = viewSize - contentSize;
		if (excess > 0.0f)
		{
			const float shift = excess * (Component(pivot, axis) - 0.5f);
			const float center = (Component(content.min, axis) + Component(content.max, axis)) * 0.5f - shift;
			Component(content.min, axis) = center - viewSize * 0.5f;
			Component(content.max, axis) = center + viewSize * 0.5f;
		}
	}
	return true;
}

XMFLOAT2 UISystem::CanvasToContentParent(const GameObject& content, XMFLOAT2 deltaCanvas) const
{
	const auto found = m_elements.find(content.GetId());
	if (found == m_elements.end()) return deltaCanvas;
	UIAffine inv;
	if (!found->second.parentToCanvas.Invert(inv)) return deltaCanvas;
	return XMFLOAT2(inv.a * deltaCanvas.x + inv.c * deltaCanvas.y, inv.b * deltaCanvas.x + inv.d * deltaCanvas.y);
}

bool UISystem::ScrollOverflow(Scene& scene, GameObject& scrollObject, const ScrollRuntime& state, XMFLOAT2& over, XMFLOAT2* viewSize) const
{
	over = XMFLOAT2(0.0f, 0.0f);
	const UIScrollRect* scroll = FindEnabled<UIScrollRect>(scrollObject);
	const GameObject* content = scroll != nullptr ? UIObjectRef::Resolve(scrollObject, scroll->content) : nullptr;
	if (content == nullptr) return false;
	const auto info = m_elements.find(content->GetId());
	UIRect view, bounds;
	if (info == m_elements.end() || !GetScrollBounds(scene, scrollObject, view, bounds)) return false;
	// 지난 Build 위치 + (지금 offset − 그때 offset) 을 캔버스로
	const UIAffine& m = info->second.parentToCanvas;
	const XMFLOAT2 d(state.offset.x - state.builtOffset.x, state.offset.y - state.builtOffset.y);
	const XMFLOAT2 moved(m.a * d.x + m.c * d.y, m.b * d.x + m.d * d.y);
	for (int axis = 0; axis < 2; ++axis)
	{
		if (axis == 0 ? !scroll->horizontal : !scroll->vertical) continue;
		const float mn = Component(bounds.min, axis) + Component(moved, axis);
		const float mx = Component(bounds.max, axis) + Component(moved, axis);
		const float maxOffset = Component(view.max, axis) - mx;
		const float minOffset = Component(view.min, axis) - mn;
		if (minOffset < -0.001f) Component(over, axis) = minOffset;
		else if (maxOffset > 0.001f) Component(over, axis) = maxOffset;
	}
	if (viewSize != nullptr)
	{
		*viewSize = XMFLOAT2(view.Width(), view.Height());
	}
	return true;
}

void UISystem::ScrollBy(Scene& scene, GameObject& scrollObject, XMFLOAT2 deltaCanvas, bool clamp)
{
	const UIScrollRect* scroll = FindEnabled<UIScrollRect>(scrollObject);
	if (scroll == nullptr) return;
	GameObject* content = UIObjectRef::Resolve(scrollObject, scroll->content);
	if (content == nullptr) return;
	ScrollRuntime& state = m_scrollState[scrollObject.GetId()];
	const XMFLOAT2 local = CanvasToContentParent(*content, deltaCanvas);
	state.offset.x += scroll->horizontal ? local.x : 0.0f;
	state.offset.y += scroll->vertical ? local.y : 0.0f;
	XMFLOAT2 over;
	if (clamp && ScrollOverflow(scene, scrollObject, state, over))
	{
		const XMFLOAT2 fix = CanvasToContentParent(*content, over);
		state.offset.x += fix.x;
		state.offset.y += fix.y;
	}
}

void UISystem::UpdateScrollRects(Scene& scene, float dt)
{
	if (dt <= 0.0f) return;
	for (const auto& owned : scene.GetObjects())
	{
		GameObject& object = *owned;
		if (object.IsMarkedForDestroy()) continue;
		const UIScrollRect* scroll = FindEnabled<UIScrollRect>(object);
		if (scroll == nullptr) continue;
		GameObject* content = UIObjectRef::Resolve(object, scroll->content);
		if (content == nullptr || m_elements.find(content->GetId()) == m_elements.end()) continue;
		ScrollRuntime& state = m_scrollState[object.GetId()];
		UIRect view, bounds;
		const bool hasBounds = GetScrollBounds(scene, object, view, bounds);
		// 지난 Build 위치 + (지금 offset − 그때 offset) → 지금 content 상자 (캔버스)
		const auto contentInfo = m_elements.find(content->GetId());
		const UIAffine& p2c = contentInfo->second.parentToCanvas;
		const auto toCanvasDelta = [&](const XMFLOAT2& d) { return XMFLOAT2(p2c.a * d.x + p2c.c * d.y, p2c.b * d.x + p2c.d * d.y); };
		const auto offsetNow = [&]()
		{
			XMFLOAT2 over;
			if (!ScrollOverflow(scene, object, state, over)) return XMFLOAT2(0.0f, 0.0f);
			return CanvasToContentParent(*content, over);
		};

		// 스크롤바를 끄는 중이면 스크롤바 값 → 위치
		GameObject* barObjects[2] = { UIObjectRef::Resolve(object, scroll->horizontalScrollbar), UIObjectRef::Resolve(object, scroll->verticalScrollbar) };
		bool barDriving = false;
		for (int axis = 0; axis < 2 && hasBounds; ++axis)
		{
			GameObject* barObject = barObjects[axis];
			if (barObject == nullptr || m_dragId != barObject->GetId()) continue;
			const UIScrollbar* bar = FindEnabled<UIScrollbar>(*barObject);
			if (bar == nullptr) continue;
			const XMFLOAT2 moved = toCanvasDelta(XMFLOAT2(state.offset.x - state.builtOffset.x, state.offset.y - state.builtOffset.y));
			const float mn = Component(bounds.min, axis) + Component(moved, axis);
			const float hidden = (Component(bounds.max, axis) - Component(bounds.min, axis)) - (Component(view.max, axis) - Component(view.min, axis));
			const float target = Component(view.min, axis) - bar->value * hidden;   // 정규 위치 v 의 content min
			XMFLOAT2 d(0.0f, 0.0f);
			Component(d, axis) = target - mn;
			const XMFLOAT2 local = CanvasToContentParent(*content, d);
			state.offset.x += local.x;
			state.offset.y += local.y;
			state.velocity = XMFLOAT2(0.0f, 0.0f);
			barDriving = true;
		}

		if (!state.dragging && !barDriving)
		{
			const XMFLOAT2 over = offsetNow();
			for (int axis = 0; axis < 2; ++axis)
			{
				float& offset = Component(state.offset, axis);
				float& velocity = Component(state.velocity, axis);
				const float o = Component(over, axis);
				if (scroll->movementType == UIScrollRect::Elastic && o != 0.0f)
				{
					float& elastic = Component(state.elasticVelocity, axis);
					offset = SmoothDamp(offset, offset + o, elastic, scroll->elasticity, dt);
					if (std::fabs(elastic) < 1.0f)
					{
						elastic = 0.0f;
					}
					velocity = 0.0f;
				}
				else if (scroll->inertia)
				{
					velocity *= std::pow(scroll->decelerationRate, dt);
					if (std::fabs(velocity) < 1.0f)
					{
						velocity = 0.0f;
					}
					offset += velocity * dt;
				}
				else
				{
					velocity = 0.0f;
				}
			}
			if (scroll->movementType == UIScrollRect::Clamped)
			{
				const XMFLOAT2 fix = offsetNow();
				state.offset.x += fix.x;
				state.offset.y += fix.y;
				if (fix.x != 0.0f) state.velocity.x = 0.0f;
				if (fix.y != 0.0f) state.velocity.y = 0.0f;
			}
		}
		else if (state.dragging)
		{
			// 드래그 중 속도 (놓은 뒤 관성)
			const XMFLOAT2 v((state.offset.x - state.prevOffset.x) / dt, (state.offset.y - state.prevOffset.y) / dt);
			const float k = (std::min)(1.0f, dt * 10.0f);
			state.velocity.x += (v.x - state.velocity.x) * k;
			state.velocity.y += (v.y - state.velocity.y) * k;
		}
		state.prevOffset = state.offset;

		// 위치 → 스크롤바 (끄는 스크롤바는 그대로)
		for (int axis = 0; axis < 2 && hasBounds; ++axis)
		{
			GameObject* barObject = barObjects[axis];
			if (barObject == nullptr || m_dragId == barObject->GetId()) continue;
			UIScrollbar* bar = FindEnabled<UIScrollbar>(*barObject);
			if (bar == nullptr) continue;
			const XMFLOAT2 moved = toCanvasDelta(XMFLOAT2(state.offset.x - state.builtOffset.x, state.offset.y - state.builtOffset.y));
			const float mn = Component(bounds.min, axis) + Component(moved, axis);
			const float contentSize = Component(bounds.max, axis) - Component(bounds.min, axis);
			const float viewSize = Component(view.max, axis) - Component(view.min, axis);
			const float hidden = contentSize - viewSize;
			const float normalized = hidden > 0.001f ? (Component(view.min, axis) - mn) / hidden : (Component(view.min, axis) > mn ? 1.0f : 0.0f);
			bar->size = contentSize > 0.0f ? (std::clamp)(viewSize / contentSize, 0.0f, 1.0f) : 1.0f;
			bar->SetValue(normalized);
		}
	}
}

void UISystem::ScrollIntoView(Scene& scene, uint64_t id)
{
	GameObject* object = scene.FindById(id);
	if (object == nullptr) return;
	const auto info = m_elements.find(id);
	if (info == m_elements.end()) return;
	for (GameObject* o = object->GetParent(); o != nullptr; o = o->GetParent())
	{
		const UIScrollRect* scroll = FindEnabled<UIScrollRect>(*o);
		if (scroll == nullptr) continue;
		GameObject* content = UIObjectRef::Resolve(*o, scroll->content);
		if (content == nullptr || !IsSelfOrAncestor(content, object)) continue;
		UIRect view, bounds;
		if (!GetScrollBounds(scene, *o, view, bounds)) return;
		ScrollRuntime& state = m_scrollState[o->GetId()];
		const UIRect elem = CanvasBounds(info->second.rect, info->second.toCanvas);
		const auto contentInfo = m_elements.find(content->GetId());
		const UIAffine& p2c = contentInfo->second.parentToCanvas;
		const XMFLOAT2 movedLocal(state.offset.x - state.builtOffset.x, state.offset.y - state.builtOffset.y);
		const XMFLOAT2 moved(p2c.a * movedLocal.x + p2c.c * movedLocal.y, p2c.b * movedLocal.x + p2c.d * movedLocal.y);
		XMFLOAT2 shift(0.0f, 0.0f);
		for (int axis = 0; axis < 2; ++axis)
		{
			if (axis == 0 ? !scroll->horizontal : !scroll->vertical) continue;
			const float mn = Component(elem.min, axis) + Component(moved, axis);
			const float mx = Component(elem.max, axis) + Component(moved, axis);
			if (mx > Component(view.max, axis)) Component(shift, axis) = Component(view.max, axis) - mx;
			if (mn + Component(shift, axis) < Component(view.min, axis)) Component(shift, axis) = Component(view.min, axis) - mn;
		}
		const XMFLOAT2 local = CanvasToContentParent(*content, shift);
		state.offset.x += local.x;
		state.offset.y += local.y;
		state.velocity = XMFLOAT2(0.0f, 0.0f);
		return;
	}
}

// ------------------------------------------------------------------ 내비게이션 (6단계)

uint64_t UISystem::FindFirstSelectable(Scene& scene) const
{
	for (uint64_t id : m_selectableOrder)
	{
		const GameObject* o = scene.FindById(id);
		const UISelectable* s = o != nullptr ? FindEnabled<UISelectable>(*o) : nullptr;
		if (s != nullptr && s->interactable && s->navigation != UISelectable::NavNone) return id;
	}
	return 0;
}

uint64_t UISystem::FindNavigationTarget(Scene& scene, uint64_t from, int dx, int dy) const
{
	GameObject* fromObject = scene.FindById(from);
	const UISelectable* s = fromObject != nullptr ? FindEnabled<UISelectable>(*fromObject) : nullptr;
	if (s == nullptr || s->navigation == UISelectable::NavNone) return 0;
	const auto usable = [&](const GameObject* o)
	{
		const UISelectable* t = o != nullptr ? FindEnabled<UISelectable>(*o) : nullptr;
		return t != nullptr && t->interactable && m_elements.find(o->GetId()) != m_elements.end();
	};
	if (s->navigation == UISelectable::NavExplicit)
	{
		const std::string& path = dx < 0 ? s->selectOnLeft : dx > 0 ? s->selectOnRight : dy > 0 ? s->selectOnUp : s->selectOnDown;
		const GameObject* target = UIObjectRef::Resolve(*fromObject, path);
		return usable(target) ? target->GetId() : 0;
	}
	if (s->navigation == UISelectable::NavHorizontal && dy != 0) return 0;
	if (s->navigation == UISelectable::NavVertical && dx != 0) return 0;

	// 유니티 Selectable.FindSelectable: 방향 쪽 가장자리 점에서 후보 중심까지의 벡터 v, dot(dir, v) > 0 인 것 중 dot / |v|² 가 가장 큰 것.
	// 화면 픽셀(y 아래 +)로 계산 — 캔버스가 달라도 같은 공간.
	XMFLOAT2 quad[4];
	if (!GetScreenQuad(fromObject, quad)) return 0;
	const auto center = [](const XMFLOAT2 q[4]) { return XMFLOAT2((q[0].x + q[1].x + q[2].x + q[3].x) * 0.25f, (q[0].y + q[1].y + q[2].y + q[3].y) * 0.25f); };
	float minX = quad[0].x, maxX = quad[0].x, minY = quad[0].y, maxY = quad[0].y;
	for (const XMFLOAT2& q : quad)
	{
		minX = (std::min)(minX, q.x);
		maxX = (std::max)(maxX, q.x);
		minY = (std::min)(minY, q.y);
		maxY = (std::max)(maxY, q.y);
	}
	const XMFLOAT2 dir(static_cast<float>(dx), static_cast<float>(-dy));
	const XMFLOAT2 c = center(quad);
	const XMFLOAT2 edge(c.x + dir.x * (maxX - minX) * 0.5f, c.y + dir.y * (maxY - minY) * 0.5f);
	uint64_t best = 0;
	float bestScore = -1.0f;
	for (uint64_t id : m_selectableOrder)
	{
		if (id == from) continue;
		const GameObject* o = scene.FindById(id);
		if (!usable(o)) continue;
		const UISelectable* t = FindEnabled<UISelectable>(*o);
		if (t->navigation == UISelectable::NavNone) continue;
		XMFLOAT2 q[4];
		if (!GetScreenQuad(o, q)) continue;
		const XMFLOAT2 oc = center(q);
		const XMFLOAT2 v(oc.x - edge.x, oc.y - edge.y);
		const float dot = dir.x * v.x + dir.y * v.y;
		if (dot <= 0.0f) continue;
		const float score = dot / (v.x * v.x + v.y * v.y);
		if (score > bestScore)
		{
			bestScore = score;
			best = id;
		}
	}
	return best;
}

void UISystem::UpdateNavigation(Scene& scene, Input& input, float dt)
{
	UIEventSystem* es = nullptr;
	GameObject* esObject = nullptr;
	for (const auto& owned : scene.GetObjects())
	{
		if (owned->IsMarkedForDestroy()) continue;
		if (UIEventSystem* found = FindEnabled<UIEventSystem>(*owned))
		{
			es = found;
			esObject = owned.get();
			break;
		}
	}
	const float repeatDelay = es != nullptr ? es->repeatDelay : 0.4f;
	const float repeatRate = es != nullptr ? (std::max)(0.01f, es->repeatRate) : 0.1f;
	const bool wasd = es != nullptr && es->wasdNavigation;

	// 선택한 요소가 사라졌거나 꺼졌으면 비움
	if (m_selectedId != 0)
	{
		const GameObject* o = scene.FindById(m_selectedId);
		const UISelectable* s = o != nullptr ? FindEnabled<UISelectable>(*o) : nullptr;
		if (s == nullptr || m_elements.find(m_selectedId) == m_elements.end())
		{
			m_selectedId = 0;
		}
	}

	// 방향: 방향키, (켜면) WASD, D-pad, 왼쪽 스틱 0.5 넘으면. 대각선이면 가로 먼저 (스틱은 큰 축)
	int dx = 0, dy = 0;
	if (input.IsKeyDown(VK_LEFT) || (wasd && input.IsKeyDown('A')) || input.IsPadDown(PadButton::DPadLeft)) dx -= 1;
	if (input.IsKeyDown(VK_RIGHT) || (wasd && input.IsKeyDown('D')) || input.IsPadDown(PadButton::DPadRight)) dx += 1;
	if (input.IsKeyDown(VK_UP) || (wasd && input.IsKeyDown('W')) || input.IsPadDown(PadButton::DPadUp)) dy += 1;
	if (input.IsKeyDown(VK_DOWN) || (wasd && input.IsKeyDown('S')) || input.IsPadDown(PadButton::DPadDown)) dy -= 1;
	if (dx == 0 && dy == 0)
	{
		const float lx = input.GetPadLeftX();
		const float ly = input.GetPadLeftY();
		if (std::fabs(lx) > 0.5f || std::fabs(ly) > 0.5f)
		{
			if (std::fabs(lx) >= std::fabs(ly)) dx = lx > 0.0f ? 1 : -1;
			else dy = ly > 0.0f ? 1 : -1;
		}
	}
	if (dx != 0)
	{
		dy = 0;
	}

	bool fire = false;
	if (dx == 0 && dy == 0)
	{
		m_navX = m_navY = 0;
		m_navRepeating = false;
	}
	else if (dx != m_navX || dy != m_navY)
	{
		m_navX = dx;
		m_navY = dy;
		m_navTimer = repeatDelay;
		fire = true;
	}
	else
	{
		m_navTimer -= dt;
		if (m_navTimer <= 0.0f)
		{
			m_navTimer += repeatRate;
			fire = true;
		}
	}

	const bool submit = input.IsKeyPressed(VK_RETURN) || input.IsKeyPressed(VK_SPACE) || input.IsPadPressed(PadButton::A);
	const bool cancel = input.IsKeyPressed(VK_ESCAPE) || input.IsPadPressed(PadButton::B);

	// 선택 표시가 꺼져 있을 때의 첫 방향·Submit 은 선택만 보이게 함 (마지막 선택 → 처음 선택할 요소 → 포인터 아래 → 처음 그린 것)
	const auto showSelection = [&]()
	{
		if (m_selectedId == 0 && es != nullptr && esObject != nullptr)
		{
			if (const GameObject* first = UIObjectRef::Resolve(*esObject, es->firstSelected); first != nullptr && FindEnabled<UISelectable>(*first) != nullptr)
			{
				m_selectedId = first->GetId();
			}
		}
		if (m_selectedId == 0 && m_hoverId != 0)
		{
			m_selectedId = m_hoverId;
		}
		if (m_selectedId == 0)
		{
			m_selectedId = FindFirstSelectable(scene);
		}
		m_selectionVisible = m_selectedId != 0;
		if (m_selectedId != 0)
		{
			ScrollIntoView(scene, m_selectedId);
		}
	};

	if (fire)
	{
		if (!m_selectionVisible || m_selectedId == 0)
		{
			showSelection();
		}
		else if (GameObject* selected = scene.FindById(m_selectedId))
		{
			// Slider 는 자기 축의 방향 입력으로 값을 바꿈 (한 칸 = 범위의 1/10, wholeNumbers 면 1)
			UISlider* slider = FindEnabled<UISlider>(*selected);
			const int sliderAxis = slider != nullptr ? (slider->direction >= UISlider::BottomToTop ? 1 : 0) : -1;
			if (slider != nullptr && slider->interactable && ((sliderAxis == 0 && dx != 0) || (sliderAxis == 1 && dy != 0)))
			{
				const bool reverse = slider->direction == UISlider::RightToLeft || slider->direction == UISlider::TopToBottom;
				const float step = slider->wholeNumbers ? 1.0f : (slider->maxValue - slider->minValue) * 0.1f;
				const int sign = (sliderAxis == 0 ? dx : dy) * (reverse ? -1 : 1);
				slider->SetValue(slider->value + step * static_cast<float>(sign));
			}
			else
			{
				const uint64_t target = FindNavigationTarget(scene, m_selectedId, dx, dy);
				if (target != 0)
				{
					m_selectedId = target;
					ScrollIntoView(scene, target);
				}
			}
		}
	}
	if (submit)
	{
		if (!m_selectionVisible || m_selectedId == 0)
		{
			showSelection();
		}
		else if (GameObject* selected = scene.FindById(m_selectedId))
		{
			Submit(scene, *selected);
		}
	}
	if (cancel && es != nullptr && esObject != nullptr)
	{
		es->onCancel.Invoke(*esObject);
	}
}

// ------------------------------------------------------------------ 색 전환

void UISystem::UpdateColors(Scene& scene, float dt)
{
	for (uint64_t id : m_selectableOrder)
	{
		const GameObject* o = scene.FindById(id);
		const UISelectable* s = o != nullptr ? FindEnabled<UISelectable>(*o) : nullptr;
		if (s == nullptr) continue;
		UISelectable::State state = UISelectable::Normal;
		if (!s->interactable)
		{
			state = UISelectable::Disabled;
		}
		else if (m_pressedId == id && m_hoverId == id)
		{
			state = UISelectable::Pressed;
		}
		else if (m_selectedId == id && m_selectionVisible)
		{
			state = UISelectable::Selected;
		}
		else if (m_hoverId == id && (m_pressedId == 0 || m_pressedId == id))
		{
			state = UISelectable::Highlighted;
		}
		const XMFLOAT4& target = s->GetStateColor(state);
		SelectableRuntime& r = m_selectableState[id];
		if (!r.initialized)
		{
			r.color = r.from = r.to = target;
			r.t = 1.0f;
			r.initialized = true;
			continue;
		}
		if (!SameColor(r.to, target))
		{
			r.from = r.color;
			r.to = target;
			r.t = 0.0f;
		}
		if (r.t < 1.0f)
		{
			r.t = s->fadeDuration > 0.0f ? (std::min)(1.0f, r.t + dt / s->fadeDuration) : 1.0f;
			r.color = LerpColor(r.from, r.to, r.t);
		}
		else
		{
			r.color = r.to;
		}
	}
}

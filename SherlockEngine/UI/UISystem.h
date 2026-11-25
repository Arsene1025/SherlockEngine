#pragma once
#include <cstdint>
#include <functional>
#include <memory_resource>
#include <string>
#include <unordered_map>
#include <vector>
#include "Core/EngineApi.h"
#include "UI/Font.h"
#include "UI/GlyphAtlas.h"
#include "UI/UIDrawList.h"
#include "UI/UITypes.h"

class Scene;
class GameObject;
class Canvas;
class Camera;
class Input;
class UISelectable;
class UIScrollRect;

// UIGraphic::Populate 가 사각형을 쌓는 곳. 바로 앞 명령과 텍스처가 같으면 같은 드로우로 묶음 (2.3).
class SHERLOCK_API UIMeshBuilder
{
public:
	// 로컬 사각형의 네 모서리를 toCanvas 로 옮겨 쌓음. uv 는 (u0, v0) = 왼쪽 위, (u1, v1) = 오른쪽 아래 (텍스처 관례)
	// glyph = 글리프 아틀라스 사각형 (텍스처 R 을 알파로, 정점 canvas 칸의 비트 16). 색에는 지금 요소의 틴트(Selectable 색 전환)를 곱하고,
	// 정점 클립 사각형은 지금 마스크 (UIRectMask 의 교집합).
	void AddQuad(const UIRect& rect, const UIAffine& toCanvas, const DirectX::XMFLOAT4& uvRect, uint32_t color, const std::string& texture, bool glyph = false);

	// 글자 배치용 (UIText::Populate)
	float GetScale() const { return m_scale; }                 // 캔버스 단위 → 출력 픽셀 (World 는 pixelsPerUnit)
	bool AllowsPixelSnap() const { return m_pixelSnap; }       // 화면 공간 캔버스 (World 는 픽셀 격자가 없음)
	FontLibrary& GetFonts() { return *m_fonts; }
	GlyphAtlas& GetAtlas() { return *m_atlas; }
	bool IsFinalPass() const { return m_finalPass; }           // 아틀라스가 넘쳐 다시 만드는 중이면 false 가 아님 — 넘친 글자를 □ 로 그릴 차례
	void ReportGlyphOverflow();                                // 마지막 시도에서도 못 넣은 글자 (경고 한 번)
	// 텍스처 픽셀 크기 (9-slice·preserveAspect·레이아웃). 모르면 false
	bool GetTextureSize(const std::string& name, uint32_t& width, uint32_t& height) const;

	// ---- UISystem 이 씀 ----
	using TextureSizeFn = std::function<bool(const std::string&, uint32_t&, uint32_t&)>;
	void Begin(UIDrawList& list, uint32_t canvasIndex);
	void BreakBatch() { m_mergeAllowed = false; }   // 다음 사각형은 새 명령으로
	void SetContext(float scale, bool pixelSnap, FontLibrary* fonts, GlyphAtlas* atlas, bool finalPass)
	{
		m_scale = scale;
		m_pixelSnap = pixelSnap;
		m_fonts = fonts;
		m_atlas = atlas;
		m_finalPass = finalPass;
	}
	void SetTextureSizeProvider(const TextureSizeFn* fn) { m_textureSize = fn; }
	void SetTint(const DirectX::XMFLOAT4& tint) { m_tint = tint; m_hasTint = tint.x != 1.0f || tint.y != 1.0f || tint.z != 1.0f || tint.w != 1.0f; }
	void SetClip(const UIRect& clip) { m_clip = clip; }
	const UIRect& GetClip() const { return m_clip; }

private:
	UIDrawList* m_list = nullptr;
	uint32_t m_canvas = 0;
	bool m_mergeAllowed = false;
	float m_scale = 1.0f;
	bool m_pixelSnap = true;
	FontLibrary* m_fonts = nullptr;
	GlyphAtlas* m_atlas = nullptr;
	bool m_finalPass = true;
	bool m_warnedOverflow = false;
	const TextureSizeFn* m_textureSize = nullptr;
	DirectX::XMFLOAT4 m_tint = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bool m_hasTint = false;
	UIRect m_clip;
};

// 게임 UI 시스템. Engine 이 하나 가짐.
//
// Build 는 편집·재생 모두 매 프레임 부름 (OnGUI 뒤, Engine::Render 앞 — 에디터가 그 프레임에 씬 창 크기와 Inspector
// 값을 바꾼 뒤): 캔버스 찾기 → World 먼저, sortOrder 정렬 → 루트 사각형(CanvasScaler) → 깊이 우선으로 RectTransform 계산
// (레이아웃 그룹·Slider·Scrollbar·ScrollRect 가 정하는 자식 사각형은 이 프레임에만 덮어씀) → Graphic 이 사각형을 쌓음.
// 결과는 CPU 그리기 목록과 메인 뷰의 히트 목록뿐이고 GPU 업로드는 Renderer::Render 안에서 함.
//
// Update 는 재생 중에만 Scene::Update 첫머리에서 부름 (PlayContext::ui): 지난 프레임의 히트 목록으로 포인터 이벤트(올림·누름·클릭·
// 드래그·휠), 키보드·패드 내비게이션, 색 페이드, 스크롤 관성. 실행 중 상태는 모두 오브젝트 id 로 갖고 BeginPlay·EndPlay 에서 비움.
class SHERLOCK_API UISystem
{
public:
	UISystem() = default;
	UISystem(const UISystem&) = delete;
	UISystem& operator=(const UISystem&) = delete;
	struct ViewSize
	{
		uint32_t width = 0;
		uint32_t height = 0;
		bool enabled = false;
		const Camera* camera = nullptr;   // World 캔버스를 그릴 카메라 (없으면 World 캔버스를 건너뜀)
	};

	void Build(const Scene& scene, ViewSize mainView, ViewSize previewView);
	const UIDrawList& GetDrawList() const { return m_drawList; }
	void SetTextureSizeProvider(std::function<bool(const std::string&, uint32_t&, uint32_t&)> fn) { m_textureSize = std::move(fn); }

	// ---- 재생 중 ----
	void BeginPlay();                                   // 실행 중 상태 비움 (Scene::BeginPlay)
	void EndPlay(Scene& scene);                         // 실행 중 상태·코드 리스너(AddListener) 비움 (Scene::EndPlay)
	void Update(Scene& scene, Input& input, float dt);  // Scene::Update 첫머리
	// 포인터가 놓인 출력 이미지의 창 클라이언트 좌표 사각형 (에디터: 씬 뷰 이미지, 게임: 창 전체). 프레임마다 정하지 않으면
	// (0, 0, 메인 뷰 크기). enabled = false 면 포인터가 없는 것으로 봄.
	void SetPointerViewport(float x, float y, float width, float height, bool enabled);

	bool IsPointerOverUI() const { return m_pointerOverUI; }   // 지난 Update 에서 포인터가 raycastTarget Graphic 위였음
	bool IsPointerCaptured() const { return m_pressedId != 0 || m_dragId != 0; }   // UI 를 누른 채 (떼기 전). 카메라 회전·에디터 선택이 피함
	bool HasSelection() const { return m_selectedId != 0; }    // 키보드·패드 선택이 있음 (방향키를 게임 이동에 쓰는 스크립트가 피할 때)
	uint64_t GetSelectedId() const { return m_selectedId; }
	void SetSelected(const GameObject* object);              // nullptr = 선택 해제
	bool IsSelectionVisible() const { return m_selectionVisible; }
	// 토글 하나를 켜거나 끔 — 그룹 규칙(같은 그룹 끄기, allowSwitchOff)까지
	void SetToggle(Scene& scene, GameObject& toggleObject, bool on);

	// ---- 에디터 ----
	// 메인 뷰 기준으로 계산한 요소의 화면 사각형 네 모서리 (출력 픽셀, 원점 왼쪽 위, y 아래 +). 순서: 왼쪽 아래, 오른쪽 아래,
	// 오른쪽 위, 왼쪽 위. 이번 프레임에 캔버스 아래에서 계산되지 않은 오브젝트면 false. World 캔버스는 투영한 사각형.
	bool GetScreenQuad(const GameObject* object, DirectX::XMFLOAT2 out[4]) const;
	// 요소의 계산된 사각형 (부모 공간)과 부모 공간 → 출력 픽셀 변환에 쓰는 정보 (Rect 툴). Overlay 캔버스만 true
	struct ElementGeometry
	{
		UIRect rect;               // 부모 공간 (회전·스케일 전)
		UIRect parentRect;         // 부모 사각형 (앵커 기준)
		UIAffine parentToCanvas;   // 부모 공간 → 캔버스
		UIAffine toCanvas;         // 자기 회전·스케일까지 → 캔버스
		float scale = 1.0f;        // 캔버스 단위 → 출력 픽셀
		uint32_t viewHeight = 0;
		bool driven[2] = {};       // 레이아웃 그룹 등이 가로·세로를 정함 (Inspector 회색)
		bool drivenAnchors = false;
	};
	bool GetElementGeometry(const GameObject* object, ElementGeometry& out) const;
	// 출력 픽셀 위치의 UI 요소 (raycastTarget 과 무관하게 사각형이 있는 모든 요소, 위에 그린 것부터). after 가 있으면 그 다음 것
	// (같은 자리 반복 클릭으로 아래 요소 고르기). 없으면 0
	uint64_t PickElement(float px, float py, uint64_t after = 0) const;

	// 오브젝트가 캔버스 아래에 있는가 (캔버스 루트 자신 포함). 에디터가 기즈모·Transform 위치 칸을 숨길 때 씀.
	static bool IsUnderCanvas(const GameObject& object);
	// 화면 공간 캔버스 아래인가 (World 캔버스 아래는 루트에 일반 기즈모를 씀)
	static bool IsUnderScreenCanvas(const GameObject& object);
	// 위로 올라가며 처음 만나는 켜진 Canvas (자기 포함). 없으면 nullptr
	static const Canvas* FindCanvas(const GameObject& object, const GameObject** canvasObject = nullptr);

	// 이번 프레임에 만든 요소 수 (통계·로그)
	uint32_t GetElementCount() const { return m_elementCount; }

private:
	struct CanvasInfo
	{
		DirectX::XMFLOAT4X4 toClip;   // 캔버스 좌표 → 클립 (행벡터)
		bool world = false;
		float scale = 1.0f;
		uint32_t viewWidth = 0, viewHeight = 0;
	};
	struct ElementInfo
	{
		uint32_t canvas = 0;       // m_canvases 번호
		UIRect rect;               // 부모 공간
		UIRect parentRect;
		UIAffine parentToCanvas;
		UIAffine toCanvas;         // rect 의 좌표 → 캔버스
		UIRect clip;               // 캔버스 좌표 (마스크 없으면 아주 큼)
		bool driven[2] = {};
		bool drivenAnchors = false;
		bool root = false;         // 캔버스 루트 (에디터 클릭 선택에서 뺌)
	};
	struct Driven
	{
		bool anchors = false;
		DirectX::XMFLOAT2 anchorMin{ 0.0f, 0.0f }, anchorMax{ 1.0f, 1.0f };
		bool axis[2] = {};          // 부모 사각형 기준 위치·크기 (x: 왼쪽에서, y: 위에서)
		float pos[2] = {};
		float size[2] = {};
		DirectX::XMFLOAT2 offset{ 0.0f, 0.0f };   // ScrollRect 가 옮긴 양 (부모 공간)
	};
	struct LayoutSize
	{
		float min = 0.0f, preferred = 0.0f, flexible = 0.0f;
	};
	struct SelectableRuntime
	{
		DirectX::XMFLOAT4 color{ 1.0f, 1.0f, 1.0f, 1.0f };   // 지금 틴트
		DirectX::XMFLOAT4 from{ 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT4 to{ 1.0f, 1.0f, 1.0f, 1.0f };
		float t = 1.0f;
		bool initialized = false;
	};
	struct ScrollRuntime
	{
		DirectX::XMFLOAT2 offset{ 0.0f, 0.0f };     // content 를 옮긴 양 (content 부모 공간)
		DirectX::XMFLOAT2 velocity{ 0.0f, 0.0f };
		DirectX::XMFLOAT2 prevOffset{ 0.0f, 0.0f };
		DirectX::XMFLOAT2 dragStartOffset{ 0.0f, 0.0f };
		DirectX::XMFLOAT2 dragStartPointer{ 0.0f, 0.0f };   // content 부모 공간
		bool dragging = false;
		DirectX::XMFLOAT2 elasticVelocity{ 0.0f, 0.0f };
		DirectX::XMFLOAT2 builtOffset{ 0.0f, 0.0f };   // 지난 Build 가 메인 뷰에 쓴 offset (그 뒤로 옮긴 양을 경계에 더함)
	};

	void BuildAll(const Scene& scene, ViewSize mainView, ViewSize previewView, bool finalPass);
	void BuildView(const Scene& scene, ViewSize view, uint32_t viewIndex);
	void Visit(const GameObject& object, const UIRect& parentRect, const UIAffine& parentToCanvas, uint32_t viewIndex, bool isRoot);
	void ApplyDrivers(const GameObject& object, const UIRect& rect);
	void LayoutChildren(const GameObject& object, const UIRect& rect);
	LayoutSize Measure(const GameObject& object, int axis, float width);
	LayoutSize MeasureGroup(const GameObject& object, int axis, float width);
	std::vector<const GameObject*> LayoutChildrenOf(const GameObject& object) const;
	void ComputeTint(const GameObject& object, const UISelectable& selectable);

	// 포인터 → 캔버스 좌표 (캔버스 행렬의 역, World 는 z = 0 평면과의 교점). w = 투영 깊이 (가까운 World 캔버스 고르기)
	bool PixelToCanvas(const CanvasInfo& canvas, float px, float py, DirectX::XMFLOAT2& out, float* w = nullptr) const;
	bool PixelToLocal(uint64_t id, float px, float py, DirectX::XMFLOAT2& local) const;   // 요소 rect 공간
	bool PixelToParent(uint64_t id, float px, float py, DirectX::XMFLOAT2& out) const;    // 요소의 부모 공간
	uint64_t HitTest(float px, float py) const;   // raycastTarget Graphic (위에 그린 것부터). 없으면 0
	bool ElementContains(const ElementInfo& e, float px, float py, float* w) const;

	// ---- Update 단계 ----
	void UpdatePointer(Scene& scene, Input& input, float dt);
	void UpdateNavigation(Scene& scene, Input& input, float dt);
	void UpdateScrollRects(Scene& scene, float dt);
	void UpdateColors(Scene& scene, float dt);
	void Click(Scene& scene, GameObject& object);
	void Submit(Scene& scene, GameObject& object);
	void DragSlider(Scene& scene, GameObject& object, float px, float py);
	void DragScrollbar(Scene& scene, GameObject& object, float px, float py, bool begin);
	void ScrollBy(Scene& scene, GameObject& scrollObject, DirectX::XMFLOAT2 deltaCanvas, bool clamp);
	bool GetScrollBounds(Scene& scene, GameObject& scrollObject, UIRect& view, UIRect& content) const;   // 캔버스 좌표 (지난 Build 위치)
	// 지금 offset 에서 content 가 뷰를 벗어난 양 (캔버스, 유니티 ScrollRect.CalculateOffset). view 크기도 돌려줌
	bool ScrollOverflow(Scene& scene, GameObject& scrollObject, const ScrollRuntime& state, DirectX::XMFLOAT2& over, DirectX::XMFLOAT2* viewSize = nullptr) const;
	DirectX::XMFLOAT2 CanvasToContentParent(const GameObject& content, DirectX::XMFLOAT2 deltaCanvas) const;
	void ScrollIntoView(Scene& scene, uint64_t id);
	uint64_t FindNavigationTarget(Scene& scene, uint64_t from, int dx, int dy) const;
	uint64_t FindFirstSelectable(Scene& scene) const;

	UIDrawList m_drawList;
	UIMeshBuilder m_builder;
	FontLibrary m_fonts;
	GlyphAtlas m_atlas;
	bool m_finalPass = true;
	static constexpr uint32_t kSaturatedRetryFrames = 120;
	bool m_atlasSaturated = false;   // 가장 큰 아틀라스로도 다 들어가지 않음
	uint32_t m_saturatedFrames = 0;
	uint32_t m_elementCount = 0;
	bool m_warnedCanvasLimit = false;
	std::function<bool(const std::string&, uint32_t&, uint32_t&)> m_textureSize;

	// 한 BuildView 동안만 (포인터 키 — 그 사이에는 오브젝트가 지워지지 않음)
	const Scene* m_buildScene = nullptr;
	uint32_t m_viewIndex = 0;
	float m_viewScale = 1.0f;
	// 아래 맵들은 Build 마다 비우고 다시 채움 — 노드를 풀에서 재사용해 요소마다 힙 할당하지 않음 (요소 1,000 개 씬 Release UI Build 0.52 → 0.48 ms)
	using MeasureCache = std::pmr::unordered_map<const GameObject*, LayoutSize>;
	std::pmr::unsynchronized_pool_resource m_framePool;
	std::pmr::unordered_map<const GameObject*, Driven> m_driven{ &m_framePool };
	std::pmr::unordered_map<const GameObject*, DirectX::XMFLOAT4> m_tints{ &m_framePool };
	std::pmr::unordered_map<const GameObject*, bool> m_hidden{ &m_framePool };
	MeasureCache m_measureCache[2]{ MeasureCache(&m_framePool), MeasureCache(&m_framePool) };

	// 메인 뷰 결과 (다음 프레임 Update·에디터가 읽음 — 오브젝트 id 키)
	std::vector<CanvasInfo> m_canvases;
	std::pmr::unordered_map<uint64_t, ElementInfo> m_elements{ &m_framePool };
	std::vector<uint64_t> m_elementOrder;   // 그린 순서 (모든 요소)
	std::vector<uint64_t> m_hitOrder;       // 그린 순서 (raycastTarget Graphic 이 있는 요소)
	std::vector<uint64_t> m_selectableOrder;   // 그린 순서 (켜진 Selectable)
	uint32_t m_mainWidth = 0, m_mainHeight = 0;

	// 포인터 영역
	float m_viewportX = 0.0f, m_viewportY = 0.0f, m_viewportW = 0.0f, m_viewportH = 0.0f;
	bool m_viewportSet = false;
	bool m_viewportEnabled = true;

	// 실행 중 상태
	std::unordered_map<uint64_t, SelectableRuntime> m_selectableState;
	std::unordered_map<uint64_t, ScrollRuntime> m_scrollState;
	uint64_t m_hoverId = 0;       // 포인터 아래 Selectable
	uint64_t m_pressedId = 0;     // 누른 Selectable (떼면 클릭 후보)
	uint64_t m_dragId = 0;        // 드래그 대상 (Slider·Scrollbar·ScrollRect)
	bool m_dragStarted = false;   // ScrollRect: 문턱을 넘어 드래그 중 (누른 버튼은 클릭 취소)
	float m_pressX = 0.0f, m_pressY = 0.0f;
	float m_scrollbarGrab = 0.0f; // Scrollbar: 손잡이 안에서 잡은 위치 (0~1 의 몇 배)
	uint64_t m_selectedId = 0;
	bool m_selectionVisible = false;
	bool m_pointerOverUI = false;
	bool m_hasPointer = false;
	float m_lastPointerX = -1.0f, m_lastPointerY = -1.0f;
	int m_navX = 0, m_navY = 0;   // 누르고 있는 방향
	float m_navTimer = 0.0f;
	bool m_navRepeating = false;
	bool m_playing = false;
};

#pragma once
#include <string>
#include "Core/EngineApi.h"   // SHERLOCK_API (엔진 DLL 내보내기)
#include "Scene/Behaviour.h"
#include "UI/UITypes.h"

class UIMeshBuilder;

// 모든 게임 UI 컴포넌트의 기반. UISystem 이 오브젝트마다 dynamic_cast 를 컴포넌트 종류 수만큼 하지 않도록 종류 번호를 줌
// (dynamic_cast 한 번 + switch). 직접 붙이지 않음.
class SHERLOCK_API UIBehaviour : public Behaviour
{
public:
	enum Kind : int
	{
		KindCanvas,
		KindCanvasScaler,
		KindRectTransform,
		KindGraphic,
		KindButton,
		KindToggle,
		KindSlider,
		KindScrollbar,
		KindToggleGroup,
		KindScrollRect,
		KindLayoutGroup,
		KindLayoutElement,
		KindContentSizeFitter,
		KindRectMask,
		KindEventSystem,
	};
	virtual Kind GetUIKind() const = 0;
};

// 게임 UI 컴포넌트 (유니티 uGUI 방식). docs/plan/ui-canvas-plan.html
//
// UI 요소도 GameObject 에 붙는 컴포넌트라 Hierarchy·Inspector·씬 저장·되돌리기·프리팹을 그대로 씀. 레이아웃과 그리기
// 목록 만들기는 Update 가 아니라 UISystem::Build 가 매 프레임 직접 함 — Scene::Update 는 재생 중에만 돌지만 UI 는 편집
// 중에도 보여야 하기 때문 (3.1).

// 캔버스 루트. 이 컴포넌트가 붙은 오브젝트(위로 Canvas 가 없는 것)가 화면 하나를 대표함.
//   Overlay: 루트 사각형 = 출력 화면 (CanvasScaler 를 거친 크기), 루트의 RectTransform 값은 무시. 장면 위에 그림.
//   World (8단계): 루트 사각형 = 루트 RectTransform 의 sizeDelta·pivot (부모 없이), 루트 GameObject 의 월드 행렬로 3D 에 놓임
//   (캔버스 1 단위 = 루트 스케일 × 1 m — 보통 스케일 0.01). 장면 깊이로 가려지고(깊이 검사만, 쓰기 없음) Overlay 보다 먼저 그림.
class SHERLOCK_API Canvas : public UIBehaviour
{
public:
	Kind GetUIKind() const override { return KindCanvas; }
	enum RenderMode : int
	{
		Overlay = 0,
		World = 1,
	};
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	int renderMode = Overlay;
	int sortOrder = 0;            // 캔버스끼리의 순서. 작은 것부터 그림 (같으면 Hierarchy 순서). World 끼리도 이 순서
	float pixelsPerUnit = 1.0f;   // World: 글자를 구울 때 캔버스 단위 하나 = 몇 픽셀 (거리에 따라 바꾸지 않음 — 아틀라스가 흔들리지 않게)
};

// 캔버스 배율. 캔버스 루트에 붙음 (없으면 Constant Pixel Size, 배율 1).
//   Scale With Screen Size: scale = 2^lerp(log2(W/refW), log2(H/refH), match) (2.2)
class SHERLOCK_API CanvasScaler : public UIBehaviour
{
public:
	Kind GetUIKind() const override { return KindCanvasScaler; }
	enum Mode : int
	{
		ConstantPixelSize = 0,
		ScaleWithScreenSize = 1,
	};
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	// 출력 크기(픽셀) → 캔버스 단위 하나가 몇 픽셀인가
	float ComputeScale(float screenWidth, float screenHeight) const;

	int mode = ScaleWithScreenSize;
	float scaleFactor = 1.0f;                                         // Constant Pixel Size
	DirectX::XMFLOAT2 referenceResolution = DirectX::XMFLOAT2(1920.0f, 1080.0f);   // Scale With Screen Size
	float match = 0.0f;                                               // 0 = 가로 맞춤, 1 = 세로 맞춤
};

// 부모 사각형 안에서 자기 사각형을 정함 (2.1). 캔버스 아래 오브젝트의 위치·크기는 이 값이 정하고, GameObject 의
// Transform 에서는 회전 z 와 스케일 xy 만 읽음 (위치는 무시) — 3.2 "RectTransform 과 Transform".
class SHERLOCK_API RectTransform : public UIBehaviour
{
public:
	Kind GetUIKind() const override { return KindRectTransform; }
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	// 부모 사각형 → 자기 사각형 (부모와 같은 공간, 회전·스케일 전). pivotPoint = 회전·스케일 중심.
	UIRect ComputeRect(const UIRect& parent, DirectX::XMFLOAT2* pivotPoint = nullptr) const;
	// 앵커만 바꿔서 계산 (Slider·Scrollbar 가 매 프레임 정하는 앵커)
	UIRect ComputeRectWithAnchors(const UIRect& parent, const DirectX::XMFLOAT2& anchorMinValue, const DirectX::XMFLOAT2& anchorMaxValue, DirectX::XMFLOAT2* pivotPoint = nullptr) const;

	DirectX::XMFLOAT2 anchorMin = DirectX::XMFLOAT2(0.5f, 0.5f);
	DirectX::XMFLOAT2 anchorMax = DirectX::XMFLOAT2(0.5f, 0.5f);
	DirectX::XMFLOAT2 pivot = DirectX::XMFLOAT2(0.5f, 0.5f);
	DirectX::XMFLOAT2 anchoredPosition = DirectX::XMFLOAT2(0.0f, 0.0f);
	DirectX::XMFLOAT2 sizeDelta = DirectX::XMFLOAT2(100.0f, 100.0f);
};

// 사각형에 무언가를 그리는 컴포넌트의 기반 (Image, Text). 등록하지 않음 (직접 붙이지 않음).
class SHERLOCK_API UIGraphic : public UIBehaviour
{
public:
	Kind GetUIKind() const override { return KindGraphic; }
	// rect: 요소 로컬 공간의 사각형, toCanvas: 로컬 → 캔버스
	virtual void Populate(UIMeshBuilder& builder, const UIRect& rect, const UIAffine& toCanvas) const = 0;

	// 레이아웃 그룹이 읽는 크기 (5단계, 캔버스 단위). width = 가로 줄바꿈 기준 너비 (세로 크기를 물을 때만 의미 있음)
	virtual DirectX::XMFLOAT2 GetPreferredSize(UIMeshBuilder& builder, float width) const
	{
		(void)builder;
		(void)width;
		return DirectX::XMFLOAT2(0.0f, 0.0f);
	}

	DirectX::XMFLOAT4 color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);   // sRGB, 텍스처에 곱함
	bool raycastTarget = true;                                              // 포인터 히트 테스트 대상 (3단계)

protected:
	void ReflectGraphic(PropertyVisitor& v);
};

// 사각형에 텍스처(없으면 흰색) × 색.
//   Simple: 사각형 하나. preserveAspect 면 텍스처 비율로 줄여 가운데.
//   Sliced (9-slice, 4단계): 텍스처 픽셀 단위 border(왼·오·위·아래)는 늘이지 않고 가운데만 늘임. 사각형이 border 합보다 작으면
//   모서리를 비율대로 줄임. border 는 텍스처 픽셀 = 캔버스 단위 (유니티 pixelsPerUnit 100 · 기준 100 과 같음).
//   Filled (4단계): fillAmount 만큼만 그림 (가로·세로, fillOrigin 0 = 왼쪽/아래, 1 = 오른쪽/위). uv 도 같이 자름.
class SHERLOCK_API UIImage : public UIGraphic
{
public:
	enum Type : int
	{
		Simple = 0,
		Sliced = 1,
		Filled = 2,
	};
	enum FillMethod : int
	{
		Horizontal = 0,
		Vertical = 1,
	};
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;
	void Populate(UIMeshBuilder& builder, const UIRect& rect, const UIAffine& toCanvas) const override;
	DirectX::XMFLOAT2 GetPreferredSize(UIMeshBuilder& builder, float width) const override;

	// 9-slice 경계 (텍스처 픽셀, x 왼 y 오 z 위 w 아래). 나중에 텍스처 임포트 설정으로 옮길 수 있게 이 함수로만 읽음
	DirectX::XMFLOAT4 GetBorder() const { return DirectX::XMFLOAT4(borderLeft, borderRight, borderTop, borderBottom); }

	std::string texture;   // TextureCache::Load2D 이름. 빈 문자열 = 흰색
	int type = Simple;
	bool preserveAspect = false;
	float borderLeft = 0.0f;
	float borderRight = 0.0f;
	float borderTop = 0.0f;
	float borderBottom = 0.0f;
	bool fillCenter = true;      // Sliced: 가운데 칸을 그림
	int fillMethod = Horizontal;
	int fillOrigin = 0;
	float fillAmount = 1.0f;
};

// 글자 (2단계). UTF-8 문자열을 글꼴로 배치해 글리프 아틀라스의 사각형으로 그림. 글리프 픽셀 크기 = round(fontSize × 캔버스 배율)
// 이라 화면 픽셀에 맞춰 구움(비트맵 글리프). 줄바꿈은 공백과 줄바꿈 문자에서, 한 낱말이 줄보다 길면 글자 단위.
class SHERLOCK_API UIText : public UIGraphic
{
public:
	enum Alignment : int
	{
		UpperLeft, UpperCenter, UpperRight,
		MiddleLeft, MiddleCenter, MiddleRight,
		LowerLeft, LowerCenter, LowerRight,
	};
	enum VerticalOverflow : int
	{
		Overflow = 0,   // 사각형 밖으로 나가도 그림
		Truncate = 1,   // 다 들어가지 않는 줄은 그리지 않음
		Ellipsis = 2,   // Truncate + 마지막 줄 끝을 "…" 로
	};

	UIText();
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;
	void Populate(UIMeshBuilder& builder, const UIRect& rect, const UIAffine& toCanvas) const override;
	// 가로 = 줄바꿈 없이 가장 긴 줄, 세로 = width 로 줄바꿈했을 때 줄 수의 높이 (Populate 와 같은 배치 코드)
	DirectX::XMFLOAT2 GetPreferredSize(UIMeshBuilder& builder, float width) const override;

	std::string text = "New Text";
	std::string font;            // FontLibrary 이름. 빈 문자열 = 엔진 기본 (Noto Sans KR)
	int fontSize = 18;           // em 크기 (캔버스 단위). 첫 줄 높이 ≈ 1.45 em (Noto Sans KR) — 기본 상자 160×30 에 들어가는 크기
	                             // (Truncate 에서는 첫 줄도 들어가지 않으면 아무것도 그리지 않음 — 유니티와 같음)
	int alignment = UpperLeft;
	float lineSpacing = 1.0f;    // 줄 높이 배율
	bool wrap = true;            // 가로 줄바꿈 (끄면 사각형 밖으로 나감)
	int verticalOverflow = Truncate;
};

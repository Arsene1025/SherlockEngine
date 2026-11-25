#pragma once
#include <DirectXMath.h>
#include "Core/EngineApi.h"
#include "Scene/Behaviour.h"
#include "UI/UIComponents.h"

// 자동 레이아웃·마스크 (docs/plan/ui-canvas-plan.html 5단계, 유니티 uGUI 의 Auto Layout 과 같은 규칙).
//
// 그룹이 정한 자식 사각형은 UISystem::Build 가 매 프레임 계산해 그 프레임에만 씀 — 자식 RectTransform 의 저장 값은
// 바꾸지 않음(유니티는 저장 값을 덮어써 열기만 해도 씬이 바뀐 것처럼 보임). Inspector 는 그룹이 정하는 칸을 회색으로 보여 줌.

// 레이아웃 그룹 공통 (직접 붙이지 않음)
class SHERLOCK_API UILayoutGroup : public UIBehaviour
{
public:
	Kind GetUIKind() const override { return KindLayoutGroup; }
	int paddingLeft = 0;
	int paddingRight = 0;
	int paddingTop = 0;
	int paddingBottom = 0;
	int childAlignment = 0;   // UIText::Alignment 와 같은 9 방향 (0 = 왼쪽 위)

protected:
	void ReflectGroup(PropertyVisitor& v);
};

// 가로·세로 한 줄 (HorizontalLayoutGroup·VerticalLayoutGroup)
class SHERLOCK_API UILinearLayout : public UILayoutGroup
{
public:
	virtual bool IsVertical() const = 0;

	float spacing = 0.0f;
	bool controlChildWidth = true;    // 자식 너비를 선호 크기로 정함 (끄면 자식 sizeDelta 그대로)
	bool controlChildHeight = true;
	bool childForceExpandWidth = false;   // 남는 공간을 자식에게 나눠 줌 (flexible 최소 1)
	bool childForceExpandHeight = false;

protected:
	void ReflectLinear(PropertyVisitor& v);
};

class SHERLOCK_API UIHorizontalLayout : public UILinearLayout
{
public:
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;
	bool IsVertical() const override { return false; }
};

class SHERLOCK_API UIVerticalLayout : public UILinearLayout
{
public:
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;
	bool IsVertical() const override { return true; }
};

// 격자 (GridLayoutGroup). 칸 크기 고정.
class SHERLOCK_API UIGridLayout : public UILayoutGroup
{
public:
	enum Constraint : int
	{
		Flexible = 0,
		FixedColumnCount = 1,
		FixedRowCount = 2,
	};
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	DirectX::XMFLOAT2 cellSize = DirectX::XMFLOAT2(100.0f, 100.0f);
	DirectX::XMFLOAT2 spacing = DirectX::XMFLOAT2(0.0f, 0.0f);
	int startCorner = 0;     // 0 왼쪽 위, 1 오른쪽 위, 2 왼쪽 아래, 3 오른쪽 아래
	int startAxis = 0;       // 0 가로로 먼저 채움, 1 세로로 먼저
	int constraint = Flexible;
	int constraintCount = 2;
};

// 요소 하나의 레이아웃 크기를 덮어씀. 음수 = 덮어쓰지 않음.
class SHERLOCK_API UILayoutElement : public UIBehaviour
{
public:
	Kind GetUIKind() const override { return KindLayoutElement; }
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	bool ignoreLayout = false;   // 그룹이 이 요소를 건너뜀 (자기 RectTransform 대로)
	float minWidth = -1.0f;
	float minHeight = -1.0f;
	float preferredWidth = -1.0f;
	float preferredHeight = -1.0f;
	float flexibleWidth = -1.0f;
	float flexibleHeight = -1.0f;
};

// 자기 크기를 내용의 min·preferred 크기로 (ScrollRect 의 content 에 씀)
class SHERLOCK_API UIContentSizeFitter : public UIBehaviour
{
public:
	Kind GetUIKind() const override { return KindContentSizeFitter; }
	enum Fit : int
	{
		Unconstrained = 0,
		MinSize = 1,
		PreferredSize = 2,
	};
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	int horizontalFit = Unconstrained;
	int verticalFit = PreferredSize;
};

// 자손을 자기 사각형(캔버스 좌표의 축 정렬 상자, padding 만큼 안쪽)으로 자름. 중첩되면 교집합. 히트 테스트도 클립 밖은 무시.
class SHERLOCK_API UIRectMask : public UIBehaviour
{
public:
	Kind GetUIKind() const override { return KindRectMask; }
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	DirectX::XMFLOAT2 padding = DirectX::XMFLOAT2(0.0f, 0.0f);   // 가로·세로 안쪽 여백 (캔버스 단위)
};

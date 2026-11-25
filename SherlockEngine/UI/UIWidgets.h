#pragma once
#include <string>
#include "Core/EngineApi.h"
#include "Scene/Behaviour.h"
#include "UI/UIComponents.h"
#include "UI/UIEvent.h"

// 누를 수 있는 게임 UI 위젯 (docs/plan/ui-canvas-plan.html 3·4·5·6단계, 유니티 uGUI 의 Selectable 계열).
//
// 컴포넌트에는 저장되는 설정과 값(Slider.value, Toggle.isOn …)만 둠. 올림·눌림·선택·색 페이드·스크롤 위치·관성 같은
// 실행 중 상태는 UISystem 이 오브젝트 id 별로 갖고, 재생을 시작·멈출 때 비움 — 씬 저장·되돌리기 스냅샷에 섞이지 않음.
// 오브젝트 참조(targetGraphic, fillRect …)는 PropertyVisitor::ObjectRef 의 상대 경로 (UIObjectRef).

// 씬에 하나 두는 설정 (없으면 기본값으로 동작). 처음 선택할 요소, 키 반복, 취소 이벤트.
class SHERLOCK_API UIEventSystem : public UIBehaviour
{
public:
	Kind GetUIKind() const override { return KindEventSystem; }
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	std::string firstSelected;       // 방향·Submit 입력이 처음 오면 고를 요소 (비면 포인터 아래 요소, 그것도 없으면 처음 그린 요소)
	float repeatDelay = 0.4f;        // 방향 입력을 누르고 있을 때 첫 반복까지
	float repeatRate = 0.1f;         // 그 뒤 반복 간격
	bool wasdNavigation = false;     // WASD 도 방향 입력으로 (게임 이동과 겹치면 끔)
	UIEvent<> onCancel;              // Esc·패드 B
};

// 상호작용 위젯의 기반 (직접 붙이지 않음). 상태별 색을 대상 Graphic 의 color 에 곱함 (fadeDuration 동안 섞음).
class SHERLOCK_API UISelectable : public UIBehaviour
{
public:
	enum Transition : int
	{
		TransitionNone = 0,
		TransitionColorTint = 1,
	};
	enum Navigation : int
	{
		NavAutomatic = 0,
		NavHorizontal = 1,
		NavVertical = 2,
		NavNone = 3,
		NavExplicit = 4,
	};
	enum State : int
	{
		Normal = 0,
		Highlighted = 1,
		Pressed = 2,
		Selected = 3,
		Disabled = 4,
	};

	bool interactable = true;
	std::string targetGraphic = ".";   // 색을 바꿀 Graphic 이 붙은 오브젝트 ("." = 자기)
	int transition = TransitionColorTint;
	DirectX::XMFLOAT4 normalColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	DirectX::XMFLOAT4 highlightedColor = DirectX::XMFLOAT4(0.961f, 0.961f, 0.961f, 1.0f);
	DirectX::XMFLOAT4 pressedColor = DirectX::XMFLOAT4(0.784f, 0.784f, 0.784f, 1.0f);
	DirectX::XMFLOAT4 selectedColor = DirectX::XMFLOAT4(0.961f, 0.961f, 0.961f, 1.0f);
	DirectX::XMFLOAT4 disabledColor = DirectX::XMFLOAT4(0.784f, 0.784f, 0.784f, 0.502f);
	float fadeDuration = 0.1f;
	int navigation = NavAutomatic;
	std::string selectOnUp, selectOnDown, selectOnLeft, selectOnRight;   // NavExplicit

	const DirectX::XMFLOAT4& GetStateColor(State state) const;

protected:
	void ReflectSelectable(PropertyVisitor& v);
};

// 누른 요소 위에서 떼면 onClick (Submit 키도)
class SHERLOCK_API UIButton : public UISelectable
{
public:
	Kind GetUIKind() const override { return KindButton; }
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	UIEvent<> onClick;
};

// 클릭으로 isOn 을 바꾸고 graphic(체크 표시)을 보이거나 숨김. group 이 있으면 그 그룹에서 하나만 켜짐.
class SHERLOCK_API UIToggle : public UISelectable
{
public:
	Kind GetUIKind() const override { return KindToggle; }
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	// 값을 바꾸고 (바뀌었으면) onValueChanged. 그룹 규칙(다른 토글 끄기, allowSwitchOff)은 UISystem 이 적용함 — 코드에서
	// 그룹 규칙까지 원하면 UISystem::SetToggle 을 쓸 것.
	void SetIsOn(bool on, bool notify = true);

	bool isOn = true;
	std::string graphic;   // 켜졌을 때만 보이는 Graphic (체크 표시)
	std::string group;     // UIToggleGroup 이 붙은 오브젝트 (비면 그룹 없음)
	UIEvent<bool> onValueChanged;
};

// 같은 그룹의 토글 중 하나만 켬
class SHERLOCK_API UIToggleGroup : public UIBehaviour
{
public:
	Kind GetUIKind() const override { return KindToggleGroup; }
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	bool allowSwitchOff = false;   // 켜진 것을 다시 눌러 모두 끌 수 있음
};

// 값 막대. fillRect 의 앵커와 handleRect 의 앵커를 값 비율로 맞춤 (Build 가 매 프레임 계산, 저장하지 않음).
class SHERLOCK_API UISlider : public UISelectable
{
public:
	Kind GetUIKind() const override { return KindSlider; }
	enum Direction : int
	{
		LeftToRight = 0,
		RightToLeft = 1,
		BottomToTop = 2,
		TopToBottom = 3,
	};
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	float Clamp(float v) const;                 // 범위·wholeNumbers 를 적용한 값
	float GetNormalizedValue() const;           // 0~1
	void SetValue(float v, bool notify = true);   // 바뀌었으면 onValueChanged

	float minValue = 0.0f;
	float maxValue = 1.0f;
	float value = 0.0f;
	bool wholeNumbers = false;
	int direction = LeftToRight;
	std::string fillRect;     // 값만큼 늘어나는 사각형
	std::string handleRect;   // 값 위치에 놓이는 손잡이. 누른 위치는 손잡이의 부모 사각형 기준 (없으면 fillRect 의 부모)
	UIEvent<float> onValueChanged;
};

// 스크롤 막대. 손잡이 크기(size) = 보이는 비율, value = 위치 (0~1)
class SHERLOCK_API UIScrollbar : public UISelectable
{
public:
	Kind GetUIKind() const override { return KindScrollbar; }
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	void SetValue(float v, bool notify = true);

	int direction = UISlider::BottomToTop;
	float value = 0.0f;
	float size = 0.2f;
	int numberOfSteps = 0;     // 0 = 연속
	std::string handleRect;
	UIEvent<float> onValueChanged;
};

// 스크롤 영역. 내용(content)을 휠·드래그로 옮기고 뷰포트(viewport, 보통 UIRectMask)로 자름.
// 옮긴 양은 UISystem 이 실행 중 상태로 가짐 — content 의 저장된 anchoredPosition 은 바뀌지 않음.
class SHERLOCK_API UIScrollRect : public UIBehaviour
{
public:
	Kind GetUIKind() const override { return KindScrollRect; }
	enum Movement : int
	{
		Unrestricted = 0,
		Elastic = 1,
		Clamped = 2,
	};
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;

	std::string content;
	std::string viewport;   // 비면 자기 사각형
	bool horizontal = false;
	bool vertical = true;
	int movementType = Elastic;
	float elasticity = 0.1f;          // 끝을 넘었을 때 돌아오는 시간 (s)
	bool inertia = true;
	float decelerationRate = 0.135f;  // 1 초 뒤 남는 속도 비율
	float scrollSensitivity = 20.0f;  // 휠 한 칸 이동량 (캔버스 단위)
	std::string horizontalScrollbar;
	std::string verticalScrollbar;
};

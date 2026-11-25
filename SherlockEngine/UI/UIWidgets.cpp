#include "pch.h"
#include "UI/UIWidgets.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	const char* const kTransitions[] = { "None", "Color Tint" };
	const char* const kNavigation[] = { "Automatic", "Horizontal", "Vertical", "None", "Explicit" };
	const char* const kDirections[] = { "Left To Right", "Right To Left", "Bottom To Top", "Top To Bottom" };
	const char* const kMovement[] = { "Unrestricted", "Elastic", "Clamped" };
}

// ------------------------------------------------------------------ UIEventSystem

void UIEventSystem::Reflect(PropertyVisitor& v)
{
	v.ObjectRef("firstSelected", firstSelected);
	v.Float("repeatDelay", repeatDelay, 0.01f);
	v.Float("repeatRate", repeatRate, 0.01f);
	v.Bool("wasdNavigation", wasdNavigation);
	v.Event("onCancel", onCancel);
}

SHERLOCK_BEHAVIOUR(UIEventSystem)

// ------------------------------------------------------------------ UISelectable

void UISelectable::ReflectSelectable(PropertyVisitor& v)
{
	v.Bool("interactable", interactable);
	v.ObjectRef("targetGraphic", targetGraphic);
	v.Enum("transition", transition, kTransitions, 2);
	v.Color("normalColor", normalColor);
	v.Color("highlightedColor", highlightedColor);
	v.Color("pressedColor", pressedColor);
	v.Color("selectedColor", selectedColor);
	v.Color("disabledColor", disabledColor);
	v.Float("fadeDuration", fadeDuration, 0.01f);
	v.Enum("navigation", navigation, kNavigation, 5);
	if (navigation == NavExplicit)
	{
		v.ObjectRef("selectOnUp", selectOnUp);
		v.ObjectRef("selectOnDown", selectOnDown);
		v.ObjectRef("selectOnLeft", selectOnLeft);
		v.ObjectRef("selectOnRight", selectOnRight);
	}
}

const XMFLOAT4& UISelectable::GetStateColor(State state) const
{
	switch (state)
	{
	case Highlighted: return highlightedColor;
	case Pressed: return pressedColor;
	case Selected: return selectedColor;
	case Disabled: return disabledColor;
	default: return normalColor;
	}
}

// ------------------------------------------------------------------ UIButton

void UIButton::Reflect(PropertyVisitor& v)
{
	ReflectSelectable(v);
	v.Event("onClick", onClick);
}

SHERLOCK_BEHAVIOUR(UIButton)

// ------------------------------------------------------------------ UIToggle / UIToggleGroup

void UIToggle::Reflect(PropertyVisitor& v)
{
	ReflectSelectable(v);
	v.Bool("isOn", isOn);
	v.ObjectRef("graphic", graphic);
	v.ObjectRef("group", group);
	v.Event("onValueChanged", onValueChanged);
}

void UIToggle::SetIsOn(bool on, bool notify)
{
	if (isOn == on) return;
	isOn = on;
	if (notify)
	{
		onValueChanged.Invoke(GetOwner(), isOn);
	}
}

SHERLOCK_BEHAVIOUR(UIToggle)

void UIToggleGroup::Reflect(PropertyVisitor& v)
{
	v.Bool("allowSwitchOff", allowSwitchOff);
}

SHERLOCK_BEHAVIOUR(UIToggleGroup)

// ------------------------------------------------------------------ UISlider

void UISlider::Reflect(PropertyVisitor& v)
{
	ReflectSelectable(v);
	v.Float("minValue", minValue, 0.01f);
	v.Float("maxValue", maxValue, 0.01f);
	v.Float("value", value, 0.01f);
	v.Bool("wholeNumbers", wholeNumbers);
	v.Enum("direction", direction, kDirections, 4);
	v.ObjectRef("fillRect", fillRect);
	v.ObjectRef("handleRect", handleRect);
	v.Event("onValueChanged", onValueChanged);
}

float UISlider::Clamp(float v) const
{
	const float lo = (std::min)(minValue, maxValue);
	const float hi = (std::max)(minValue, maxValue);
	float r = (std::clamp)(v, lo, hi);
	if (wholeNumbers)
	{
		r = std::round(r);
	}
	return r;
}

float UISlider::GetNormalizedValue() const
{
	if (std::fabs(maxValue - minValue) < 1e-6f) return 0.0f;
	return (std::clamp)((Clamp(value) - minValue) / (maxValue - minValue), 0.0f, 1.0f);
}

void UISlider::SetValue(float v, bool notify)
{
	const float clamped = Clamp(v);
	if (clamped == value) return;
	value = clamped;
	if (notify)
	{
		onValueChanged.Invoke(GetOwner(), value);
	}
}

SHERLOCK_BEHAVIOUR(UISlider)

// ------------------------------------------------------------------ UIScrollbar

void UIScrollbar::Reflect(PropertyVisitor& v)
{
	ReflectSelectable(v);
	v.Enum("direction", direction, kDirections, 4);
	v.Float("value", value, 0.01f);
	v.Float("size", size, 0.01f);
	v.Int("numberOfSteps", numberOfSteps);
	v.ObjectRef("handleRect", handleRect);
	v.Event("onValueChanged", onValueChanged);
}

void UIScrollbar::SetValue(float v, bool notify)
{
	float clamped = (std::clamp)(v, 0.0f, 1.0f);
	if (numberOfSteps > 1)
	{
		clamped = std::round(clamped * (numberOfSteps - 1)) / static_cast<float>(numberOfSteps - 1);
	}
	if (clamped == value) return;
	value = clamped;
	if (notify)
	{
		onValueChanged.Invoke(GetOwner(), value);
	}
}

SHERLOCK_BEHAVIOUR(UIScrollbar)

// ------------------------------------------------------------------ UIScrollRect

void UIScrollRect::Reflect(PropertyVisitor& v)
{
	v.ObjectRef("content", content);
	v.ObjectRef("viewport", viewport);
	v.Bool("horizontal", horizontal);
	v.Bool("vertical", vertical);
	v.Enum("movementType", movementType, kMovement, 3);
	v.Float("elasticity", elasticity, 0.01f);
	v.Bool("inertia", inertia);
	v.Float("decelerationRate", decelerationRate, 0.005f);
	v.Float("scrollSensitivity", scrollSensitivity, 0.5f);
	v.ObjectRef("horizontalScrollbar", horizontalScrollbar);
	v.ObjectRef("verticalScrollbar", verticalScrollbar);
}

SHERLOCK_BEHAVIOUR(UIScrollRect)

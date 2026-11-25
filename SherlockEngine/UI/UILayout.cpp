#include "pch.h"
#include "UI/UILayout.h"

namespace
{
	const char* const kAlignment[] = { "Upper Left", "Upper Center", "Upper Right", "Middle Left", "Middle Center", "Middle Right", "Lower Left", "Lower Center", "Lower Right" };
	const char* const kCorners[] = { "Upper Left", "Upper Right", "Lower Left", "Lower Right" };
	const char* const kAxes[] = { "Horizontal", "Vertical" };
	const char* const kConstraints[] = { "Flexible", "Fixed Column Count", "Fixed Row Count" };
	const char* const kFits[] = { "Unconstrained", "Min Size", "Preferred Size" };
}

void UILayoutGroup::ReflectGroup(PropertyVisitor& v)
{
	v.Int("paddingLeft", paddingLeft);
	v.Int("paddingRight", paddingRight);
	v.Int("paddingTop", paddingTop);
	v.Int("paddingBottom", paddingBottom);
	v.Enum("childAlignment", childAlignment, kAlignment, 9);
}

void UILinearLayout::ReflectLinear(PropertyVisitor& v)
{
	ReflectGroup(v);
	v.Float("spacing", spacing, 1.0f);
	v.Bool("controlChildWidth", controlChildWidth);
	v.Bool("controlChildHeight", controlChildHeight);
	v.Bool("childForceExpandWidth", childForceExpandWidth);
	v.Bool("childForceExpandHeight", childForceExpandHeight);
}

void UIHorizontalLayout::Reflect(PropertyVisitor& v)
{
	ReflectLinear(v);
}

SHERLOCK_BEHAVIOUR(UIHorizontalLayout)

void UIVerticalLayout::Reflect(PropertyVisitor& v)
{
	ReflectLinear(v);
}

SHERLOCK_BEHAVIOUR(UIVerticalLayout)

void UIGridLayout::Reflect(PropertyVisitor& v)
{
	ReflectGroup(v);
	v.Float2("cellSize", cellSize, 1.0f);
	v.Float2("spacing", spacing, 1.0f);
	v.Enum("startCorner", startCorner, kCorners, 4);
	v.Enum("startAxis", startAxis, kAxes, 2);
	v.Enum("constraint", constraint, kConstraints, 3);
	v.Int("constraintCount", constraintCount);
}

SHERLOCK_BEHAVIOUR(UIGridLayout)

void UILayoutElement::Reflect(PropertyVisitor& v)
{
	v.Bool("ignoreLayout", ignoreLayout);
	v.Float("minWidth", minWidth, 1.0f);
	v.Float("minHeight", minHeight, 1.0f);
	v.Float("preferredWidth", preferredWidth, 1.0f);
	v.Float("preferredHeight", preferredHeight, 1.0f);
	v.Float("flexibleWidth", flexibleWidth, 0.01f);
	v.Float("flexibleHeight", flexibleHeight, 0.01f);
}

SHERLOCK_BEHAVIOUR(UILayoutElement)

void UIContentSizeFitter::Reflect(PropertyVisitor& v)
{
	v.Enum("horizontalFit", horizontalFit, kFits, 3);
	v.Enum("verticalFit", verticalFit, kFits, 3);
}

SHERLOCK_BEHAVIOUR(UIContentSizeFitter)

void UIRectMask::Reflect(PropertyVisitor& v)
{
	v.Float2("padding", padding, 1.0f);
}

SHERLOCK_BEHAVIOUR(UIRectMask)

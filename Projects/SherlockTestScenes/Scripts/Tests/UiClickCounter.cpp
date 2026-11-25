#include "pch.h"
#include "UiClickCounter.h"
#include <cmath>
#include <format>
#include "Core/Log.h"
#include "UI/UIComponents.h"
#include "UI/UIEvent.h"

void UiClickCounter::Reflect(PropertyVisitor& v)
{
	v.ObjectRef("label", label);
	v.String("prefix", prefix);
	v.Int("count", count);
	v.Bool("writeOnStart", writeOnStart);
}

void UiClickCounter::Start()
{
	if (!writeOnStart) return;
	SetLabel(std::format("{}: {}", prefix, count));
}

void UiClickCounter::Increment()
{
	++count;
	Log::Info("UiClickCounter '%s': count %d", GetOwner().GetName().c_str(), count);
	SetLabel(std::format("{}: {}", prefix, count));
}

void UiClickCounter::ResetCount()
{
	count = 0;
	Log::Info("UiClickCounter '%s': reset", GetOwner().GetName().c_str());
	SetLabel(std::format("{}: {}", prefix, count));
}

void UiClickCounter::ShowValue(float value)
{
	Log::Info("UiClickCounter '%s': value %.3f", GetOwner().GetName().c_str(), value);
	// wholeNumbers 슬라이더는 정수로 보임
	SetLabel(value == std::floor(value) ? std::format("{}: {}", prefix, static_cast<int>(value)) : std::format("{}: {:.2f}", prefix, value));
}

void UiClickCounter::ShowFlag(bool on)
{
	Log::Info("UiClickCounter '%s': flag %s", GetOwner().GetName().c_str(), on ? "on" : "off");
	SetLabel(std::format("{}: {}", prefix, on ? "ON" : "OFF"));
}

void UiClickCounter::Say(const std::string& text)
{
	Log::Info("UiClickCounter '%s': say %s", GetOwner().GetName().c_str(), text.c_str());
	SetLabel(text);
}

void UiClickCounter::SetLabel(const std::string& text)
{
	GameObject* target = UIObjectRef::Resolve(GetOwner(), label);
	UIText* t = target != nullptr ? target->GetBehaviour<UIText>() : nullptr;
	if (t != nullptr)
	{
		t->text = text;
	}
}

SHERLOCK_SCRIPT(UiClickCounter)
SHERLOCK_UI_ACTION(UiClickCounter, Increment)
SHERLOCK_UI_ACTION(UiClickCounter, ResetCount)
SHERLOCK_UI_ACTION(UiClickCounter, ShowValue)
SHERLOCK_UI_ACTION(UiClickCounter, ShowFlag)
SHERLOCK_UI_ACTION(UiClickCounter, Say)

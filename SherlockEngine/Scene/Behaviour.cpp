#include "pch.h"
#include "Scene/Behaviour.h"
#include "Scene/GameObject.h"
#include "Core/Log.h"
#include <algorithm>
#include <map>

Transform& Behaviour::GetTransform()
{
	return m_owner->GetTransform();
}

namespace
{
	struct Entry
	{
		BehaviourRegistry::Factory factory;
		bool isScript = false;
	};
	// 정적 초기화 순서 문제를 피하려고 함수 안의 static 을 사용함 (Meyers singleton). 등록은 매크로가 만든 정적 객체가 하므로 main 보다 먼저 일어남.
	std::map<std::string, Entry>& Entries()
	{
		static std::map<std::string, Entry> entries;
		return entries;
	}
	std::vector<std::string>& Names()
	{
		static std::vector<std::string> names;
		return names;
	}
}

void BehaviourRegistry::Register(const char* typeName, Factory factory, bool isScript)
{
	// 2026-10-08: 같은 이름은 먼저 등록한 쪽이 이김. 스크립트 DLL(B안)이 엔진 컴포넌트와 같은 이름을 쓰면 무시되고 경고가 남음 —
	// 덮어쓰면 DLL 을 내릴 때(Unregister) 엔진 컴포넌트까지 지워지기 때문.
	if (!Entries().try_emplace(typeName, Entry{ std::move(factory), isScript }).second)
	{
		Log::Warn("컴포넌트 이름 '%s' 가 이미 등록돼 있어 새 등록을 무시함 (스크립트 이름을 바꿀 것).", typeName);
		return;
	}
	Names().clear();   // 다음 GetTypeNames 호출 때 다시 만듦
}

void BehaviourRegistry::Unregister(const std::string& typeName)
{
	if (Entries().erase(typeName) > 0) Names().clear();
}

std::unique_ptr<Behaviour> BehaviourRegistry::Create(const std::string& typeName)
{
	auto found = Entries().find(typeName);
	if (found == Entries().end())
	{
		Log::Warn("컴포넌트 '%s' 를 모른다 (SHERLOCK_SCRIPT / SHERLOCK_BEHAVIOUR 로 등록된 클래스가 아님 — 프로젝트 스크립트라면 스크립트 DLL 을 빌드해야 함).", typeName.c_str());
		return nullptr;
	}
	return found->second.factory();
}

const std::vector<std::string>& BehaviourRegistry::GetTypeNames()
{
	std::vector<std::string>& names = Names();
	if (names.empty())
	{
		for (const auto& entry : Entries()) names.push_back(entry.first);   // std::map 이라 이미 정렬돼 있음
	}
	return names;
}

bool BehaviourRegistry::IsScript(const std::string& typeName)
{
	auto found = Entries().find(typeName);
	return found != Entries().end() && found->second.isScript;
}

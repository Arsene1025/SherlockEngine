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
	Entries()[typeName] = Entry{ std::move(factory), isScript };
	Names().clear();   // 다음 GetTypeNames 호출 때 다시 만듦
}

std::unique_ptr<Behaviour> BehaviourRegistry::Create(const std::string& typeName)
{
	auto found = Entries().find(typeName);
	if (found == Entries().end())
	{
		Log::Warn("컴포넌트 '%s' 를 모른다 (Game/ 에 SHERLOCK_SCRIPT / SHERLOCK_BEHAVIOUR 로 등록된 클래스가 아니다). 건너뜀.", typeName.c_str());
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

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
	// 정적 초기화 순서에 안전하도록 함수 안의 static (Meyers singleton). 등록은 매크로의 정적 객체가 하므로 main 전에 온다.
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
	Names().clear();   // 다음 GetTypeNames 에서 다시 만든다
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
		for (const auto& entry : Entries()) names.push_back(entry.first);   // std::map 이라 이미 정렬
	}
	return names;
}

bool BehaviourRegistry::IsScript(const std::string& typeName)
{
	auto found = Entries().find(typeName);
	return found != Entries().end() && found->second.isScript;
}

#include "pch.h"
#include "UI/UIEvent.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include "Core/Log.h"
#include "Scene/GameObject.h"
#include "Scene/Scene.h"

const char* UIValueKindName(UIValue::Kind kind)
{
	switch (kind)
	{
	case UIValue::Float: return "float";
	case UIValue::Int: return "int";
	case UIValue::Bool: return "bool";
	case UIValue::String: return "string";
	default: return "none";
	}
}

std::string UIEventEncode(const UIEventBase& e)
{
	return e.Encode();
}

void UIEventDecode(UIEventBase& e, const std::string& text)
{
	e.Decode(text);
}

// ------------------------------------------------------------------ 저장

std::string UIEventBase::Encode() const
{
	nlohmann::json array = nlohmann::json::array();
	for (const UIPersistentCall& call : calls)
	{
		nlohmann::json j;
		j["target"] = call.target;
		j["method"] = call.method;
		j["dynamic"] = call.dynamic;
		switch (call.argument.kind)
		{
		case UIValue::Float: j["float"] = call.argument.f; break;
		case UIValue::Int: j["int"] = call.argument.i; break;
		case UIValue::Bool: j["bool"] = call.argument.b; break;
		case UIValue::String: j["string"] = call.argument.s; break;
		default: break;
		}
		array.push_back(std::move(j));
	}
	return array.dump();
}

void UIEventBase::Decode(const std::string& text)
{
	calls.clear();
	nlohmann::json array;
	try
	{
		array = nlohmann::json::parse(text);
	}
	catch (...)
	{
		return;
	}
	if (!array.is_array()) return;
	for (const nlohmann::json& j : array)
	{
		if (!j.is_object()) continue;
		UIPersistentCall call;
		call.target = j.value("target", "");
		call.method = j.value("method", "");
		call.dynamic = j.value("dynamic", true);
		if (j.contains("float") && j["float"].is_number())
		{
			call.argument = UIValue::Of(j["float"].get<float>());
		}
		else if (j.contains("int") && j["int"].is_number_integer())
		{
			call.argument = UIValue::Of(j["int"].get<int>());
		}
		else if (j.contains("bool") && j["bool"].is_boolean())
		{
			call.argument = UIValue::Of(j["bool"].get<bool>());
		}
		else if (j.contains("string") && j["string"].is_string())
		{
			call.argument = UIValue::Of(j["string"].get<std::string>());
		}
		calls.push_back(std::move(call));
	}
}

// ------------------------------------------------------------------ 호출

void UIEventBase::InvokePersistent(GameObject& owner, const UIValue& value) const
{
	// 연결 목록을 복사해 둠: 불린 함수가 이 이벤트를 가진 컴포넌트를 바꿀 수 있음
	const std::vector<UIPersistentCall> copy = calls;
	for (const UIPersistentCall& call : copy)
	{
		if (call.method.empty()) continue;
		const UIActionRegistry::Action* action = UIActionRegistry::Find(call.method);
		if (action == nullptr)
		{
			static std::map<std::string, bool> warned;
			if (!warned[call.method])
			{
				warned[call.method] = true;
				Log::Warn("UI 이벤트: 등록되지 않은 함수 '%s' (오브젝트 '%s') — SHERLOCK_UI_ACTION 으로 등록했는지, 스크립트 DLL 이 올라왔는지 확인", call.method.c_str(), owner.GetName().c_str());
			}
			continue;
		}
		GameObject* target = UIObjectRef::Resolve(owner, call.target);
		if (target == nullptr) continue;
		Behaviour* behaviour = nullptr;
		for (const auto& b : target->GetBehaviours())
		{
			if (action->type == b->GetTypeName())
			{
				behaviour = b.get();
				break;
			}
		}
		if (behaviour == nullptr) continue;
		UIValue argument = call.argument;
		if (call.dynamic && action->argument == value.kind)
		{
			argument = value;
		}
		else
		{
			argument.kind = action->argument;   // 저장된 값이 다른 종류면 기본값으로
		}
		action->invoke(*behaviour, argument);
	}
}

// ------------------------------------------------------------------ 등록표

namespace
{
	std::map<std::string, UIActionRegistry::Action>& Actions()
	{
		static std::map<std::string, UIActionRegistry::Action> actions;   // 이름순 (Inspector 콤보)
		return actions;
	}
}

void UIActionRegistry::Register(const char* type, const char* method, UIValue::Kind argument, Invoker invoke)
{
	Action action;
	action.type = type;
	action.method = method;
	action.fullName = action.type + "." + action.method;
	action.argument = argument;
	action.invoke = std::move(invoke);
	Actions()[action.fullName] = std::move(action);   // 핫리로드로 다시 올라오면 새 호출기로 바꿈
}

void UIActionRegistry::RemoveType(const std::string& type)
{
	std::erase_if(Actions(), [&type](const auto& entry) { return entry.second.type == type; });
}

const UIActionRegistry::Action* UIActionRegistry::Find(const std::string& fullName)
{
	const auto found = Actions().find(fullName);
	return found != Actions().end() ? &found->second : nullptr;
}

std::vector<const UIActionRegistry::Action*> UIActionRegistry::ListFor(const std::string& type)
{
	std::vector<const Action*> list;
	for (const auto& [name, action] : Actions())
	{
		if (action.type == type)
		{
			list.push_back(&action);
		}
	}
	return list;
}

// ------------------------------------------------------------------ 상대 경로

namespace
{
	const Scene* g_sceneContext = nullptr;
}

void UIObjectRef::SetSceneContext(const Scene* scene)
{
	g_sceneContext = scene;
}

const GameObject* UIObjectRef::Resolve(const GameObject& from, const std::string& path)
{
	if (path.empty()) return nullptr;
	const GameObject* current = &from;
	bool atSceneRoot = false;   // 루트에서 ".." 를 한 번 더 → 씬 루트 (이름 = 루트 오브젝트)
	size_t start = 0;
	while (start <= path.size())
	{
		const size_t slash = path.find('/', start);
		const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		if (part == "..")
		{
			if (atSceneRoot) return nullptr;
			current = current->GetParent();
			atSceneRoot = current == nullptr;
		}
		else if (!part.empty() && part != ".")
		{
			const GameObject* next = nullptr;
			if (atSceneRoot)
			{
				if (g_sceneContext == nullptr) return nullptr;
				for (const auto& owned : g_sceneContext->GetObjects())
				{
					if (owned->GetParent() == nullptr && owned->GetName() == part && !owned->IsMarkedForDestroy())
					{
						next = owned.get();
						break;
					}
				}
				atSceneRoot = false;
			}
			else
			{
				for (const GameObject* child : current->GetChildren())
				{
					if (child->GetName() == part && !child->IsMarkedForDestroy())
					{
						next = child;
						break;
					}
				}
			}
			if (next == nullptr) return nullptr;
			current = next;
		}
		if (slash == std::string::npos) break;
		start = slash + 1;
	}
	return atSceneRoot ? nullptr : current;
}

GameObject* UIObjectRef::Resolve(GameObject& from, const std::string& path)
{
	return const_cast<GameObject*>(Resolve(static_cast<const GameObject&>(from), path));
}

std::string UIObjectRef::MakePath(const GameObject& from, const GameObject& to)
{
	if (&from == &to) return ".";
	// to 의 조상 목록 (자기 포함)
	std::vector<const GameObject*> toChain;
	for (const GameObject* o = &to; o != nullptr; o = o->GetParent())
	{
		toChain.push_back(o);
	}
	std::string path;
	const auto appendDown = [&](std::vector<const GameObject*>::const_reverse_iterator begin)
	{
		for (auto down = begin; down != toChain.crend(); ++down)
		{
			if (!path.empty())
			{
				path += '/';
			}
			path += (*down)->GetName();
		}
	};
	const GameObject* up = &from;
	while (up != nullptr)
	{
		const auto it = std::find(toChain.begin(), toChain.end(), up);
		if (it != toChain.end())
		{
			// up 이 공통 조상: 거기서 to 까지 내려감
			appendDown(std::make_reverse_iterator(it));
			return path.empty() ? "." : path;
		}
		if (!path.empty())
		{
			path += '/';
		}
		path += "..";
		up = up->GetParent();
	}
	// 다른 루트 아래: 씬 루트까지 올라가 그 루트부터 내려감
	appendDown(toChain.crbegin());
	return path;
}

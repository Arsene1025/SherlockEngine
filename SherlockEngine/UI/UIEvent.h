#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>
#include "Core/EngineApi.h"
#include "Scene/Behaviour.h"

class GameObject;
class Scene;

// 게임 UI 이벤트 (docs/plan/ui-canvas-plan.html 3단계, 유니티 UnityEvent 와 같은 모양).
//
// 이벤트 하나 = 코드 리스너 목록(AddListener, 저장 안 됨) + 저장되는 연결 목록(Inspector 에서 고른 "대상 오브젝트 + 컴포넌트.함수").
// 저장되는 연결은 이름으로 함수를 찾아 부름 — 함수는 SHERLOCK_UI_ACTION(클래스, 함수) 로 등록함. 등록표는 엔진 DLL 에 한 벌,
// 스크립트 DLL 이 등록한 것은 ScriptModule::Unload 가 타입 이름으로 지움 (호출기가 DLL 코드를 가리키기 때문).

// 인자 값. 연결의 고정 인자와 이벤트가 넘기는 값(동적 인자)을 같은 모양으로 담음.
struct UIValue
{
	enum Kind : int
	{
		None = 0,
		Float = 1,
		Int = 2,
		Bool = 3,
		String = 4,
	};
	Kind kind = None;
	float f = 0.0f;
	int i = 0;
	bool b = false;
	std::string s;

	static UIValue Of() { return UIValue{}; }
	static UIValue Of(float v) { UIValue r; r.kind = Float; r.f = v; return r; }
	static UIValue Of(int v) { UIValue r; r.kind = Int; r.i = v; return r; }
	static UIValue Of(bool v) { UIValue r; r.kind = Bool; r.b = v; return r; }
	static UIValue Of(const std::string& v) { UIValue r; r.kind = String; r.s = v; return r; }
};

SHERLOCK_API const char* UIValueKindName(UIValue::Kind kind);

// 저장되는 연결 하나
struct UIPersistentCall
{
	std::string target;          // 이벤트를 가진 오브젝트 기준 상대 경로 ("." = 자기, "..", "Label", "../Panel/Label")
	std::string method;          // "타입.함수" (예: "UiClickCounter.Increment")
	bool dynamic = true;         // 이벤트가 넘기는 값을 씀 (함수 인자 종류가 이벤트 값 종류와 같을 때). 아니면 argument
	UIValue argument;            // 고정 인자
};

// 이벤트들의 공통 부분 (저장·Inspector 가 다룸)
class SHERLOCK_API UIEventBase
{
public:
	explicit UIEventBase(UIValue::Kind valueKind)
		: m_valueKind(valueKind)
	{}
	UIValue::Kind GetValueKind() const { return m_valueKind; }   // 이 이벤트가 리스너에게 넘기는 값의 종류

	std::vector<UIPersistentCall> calls;

	// JSON 배열 텍스트 ↔ calls (SceneSerializer·기본 방문자가 씀)
	std::string Encode() const;
	void Decode(const std::string& text);

protected:
	// 저장되는 연결을 차례로 부름. owner = 이벤트를 가진 컴포넌트의 오브젝트
	void InvokePersistent(GameObject& owner, const UIValue& value) const;

private:
	UIValue::Kind m_valueKind;
};

// 리스너 하나를 붙였다 뗄 때 쓰는 번호
using UIListenerId = uint32_t;

template <typename T>
struct UIValueKindOf;
template <> struct UIValueKindOf<void> { static constexpr UIValue::Kind value = UIValue::None; };
template <> struct UIValueKindOf<float> { static constexpr UIValue::Kind value = UIValue::Float; };
template <> struct UIValueKindOf<int> { static constexpr UIValue::Kind value = UIValue::Int; };
template <> struct UIValueKindOf<bool> { static constexpr UIValue::Kind value = UIValue::Bool; };
template <> struct UIValueKindOf<std::string> { static constexpr UIValue::Kind value = UIValue::String; };

// 값을 넘기는 이벤트 (Slider 의 float, Toggle 의 bool). T = void 는 아래 특수화 (Button.onClick).
template <typename T = void>
class UIEvent : public UIEventBase
{
public:
	UIEvent()
		: UIEventBase(UIValueKindOf<T>::value)
	{}
	UIListenerId AddListener(std::function<void(T)> fn)
	{
		m_listeners.push_back({ ++m_nextId, std::move(fn) });
		return m_nextId;
	}
	void RemoveListener(UIListenerId id)
	{
		std::erase_if(m_listeners, [id](const Listener& l) { return l.id == id; });
	}
	void RemoveAllListeners() { m_listeners.clear(); }
	void Invoke(GameObject& owner, T value)
	{
		const auto copy = m_listeners;   // 리스너 안에서 붙이거나 떼도 안전하게
		for (const Listener& l : copy)
		{
			l.fn(value);
		}
		InvokePersistent(owner, UIValue::Of(value));
	}

private:
	struct Listener
	{
		UIListenerId id;
		std::function<void(T)> fn;
	};
	std::vector<Listener> m_listeners;
	UIListenerId m_nextId = 0;
};

template <>
class UIEvent<void> : public UIEventBase
{
public:
	UIEvent()
		: UIEventBase(UIValue::None)
	{}
	UIListenerId AddListener(std::function<void()> fn)
	{
		m_listeners.push_back({ ++m_nextId, std::move(fn) });
		return m_nextId;
	}
	void RemoveListener(UIListenerId id)
	{
		std::erase_if(m_listeners, [id](const Listener& l) { return l.id == id; });
	}
	void RemoveAllListeners() { m_listeners.clear(); }
	void Invoke(GameObject& owner)
	{
		const auto copy = m_listeners;
		for (const Listener& l : copy)
		{
			l.fn();
		}
		InvokePersistent(owner, UIValue{});
	}

private:
	struct Listener
	{
		UIListenerId id;
		std::function<void()> fn;
	};
	std::vector<Listener> m_listeners;
	UIListenerId m_nextId = 0;
};

// ---- 함수 등록표 ----
namespace UIActionRegistry
{
	using Invoker = std::function<void(Behaviour&, const UIValue&)>;
	struct Action
	{
		std::string type;        // 컴포넌트 타입 이름 (GetTypeName)
		std::string method;      // 함수 이름
		std::string fullName;    // "type.method"
		UIValue::Kind argument;  // 함수가 받는 인자 (None = 인자 없음)
		Invoker invoke;
	};
	SHERLOCK_API void Register(const char* type, const char* method, UIValue::Kind argument, Invoker invoke);
	SHERLOCK_API void RemoveType(const std::string& type);   // 스크립트 DLL 을 내리기 전에
	SHERLOCK_API const Action* Find(const std::string& fullName);
	SHERLOCK_API std::vector<const Action*> ListFor(const std::string& type);

	// 멤버 함수 포인터에서 인자 종류를 알아내 등록 (SHERLOCK_UI_ACTION 이 부름). 호출기는 부르는 쪽 DLL 에 만들어짐.
	template <typename C>
	void RegisterMember(const char* type, const char* method, void (C::*fn)())
	{
		Register(type, method, UIValue::None, [fn](Behaviour& b, const UIValue&) { (static_cast<C&>(b).*fn)(); });
	}
	template <typename C>
	void RegisterMember(const char* type, const char* method, void (C::*fn)(float))
	{
		Register(type, method, UIValue::Float, [fn](Behaviour& b, const UIValue& v) { (static_cast<C&>(b).*fn)(v.f); });
	}
	template <typename C>
	void RegisterMember(const char* type, const char* method, void (C::*fn)(int))
	{
		Register(type, method, UIValue::Int, [fn](Behaviour& b, const UIValue& v) { (static_cast<C&>(b).*fn)(v.i); });
	}
	template <typename C>
	void RegisterMember(const char* type, const char* method, void (C::*fn)(bool))
	{
		Register(type, method, UIValue::Bool, [fn](Behaviour& b, const UIValue& v) { (static_cast<C&>(b).*fn)(v.b); });
	}
	template <typename C>
	void RegisterMember(const char* type, const char* method, void (C::*fn)(const std::string&))
	{
		Register(type, method, UIValue::String, [fn](Behaviour& b, const UIValue& v) { (static_cast<C&>(b).*fn)(v.s); });
	}
}

// 컴포넌트의 공개 멤버 함수를 Inspector 의 이벤트 연결 목록에 올림. 인자는 없음·float·int·bool·const std::string& 하나.
//   SHERLOCK_UI_ACTION(UiClickCounter, Increment)
#define SHERLOCK_UI_ACTION(ClassName, Method) \
	namespace \
	{ \
		struct ClassName##_##Method##_UIAction \
		{ \
			ClassName##_##Method##_UIAction() { UIActionRegistry::RegisterMember<ClassName>(#ClassName, #Method, &ClassName::Method); } \
		} s_##ClassName##_##Method##_UIAction; \
	}

// ---- 오브젝트 상대 경로 (PropertyVisitor::ObjectRef 값) ----
namespace UIObjectRef
{
	// "." = from, ".." = 부모, 이름 = 그 이름의 첫 자식. '/' 로 이음. 루트에서 ".." 를 더 하면 씬 루트(다음 이름 = 루트 오브젝트).
	// 빈 문자열이거나 못 찾으면 nullptr
	SHERLOCK_API GameObject* Resolve(GameObject& from, const std::string& path);
	SHERLOCK_API const GameObject* Resolve(const GameObject& from, const std::string& path);
	// from → to 의 상대 경로 (같은 씬 안). 같으면 "."
	SHERLOCK_API std::string MakePath(const GameObject& from, const GameObject& to);
	// 씬 루트(".." 가 루트를 넘을 때)를 찾을 씬. UISystem 이 Build·Update 앞에 정함 (게임 스레드 하나)
	SHERLOCK_API void SetSceneContext(const Scene* scene);
}

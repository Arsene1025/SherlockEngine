#pragma once
#include "Core/EngineApi.h"   // SHERLOCK_API (엔진 DLL 내보내기)
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <DirectXMath.h>

class GameObject;
class Transform;
class Scene;
class Input;
class Camera;
class UISystem;
class UIEventBase;

// UI 이벤트의 저장되는 연결 ↔ JSON 배열 텍스트 (UI/UIEvent.cpp). PropertyVisitor::Event 의 기본 구현이 씀.
SHERLOCK_API std::string UIEventEncode(const UIEventBase& e);
SHERLOCK_API void UIEventDecode(UIEventBase& e, const std::string& text);

// 모든 컴포넌트의 기반 클래스. 유니티의 Component 에 해당함.
//
// 다음 두 종류가 이 클래스를 상속함:
//   - 엔진 컴포넌트 (Scene/): Rigidbody, CameraComponent, FollowTarget — SHERLOCK_BEHAVIOUR 로 등록
//   - 스크립트 (<프로젝트>\Scripts\): 사용자가 오브젝트의 움직임·규칙을 직접 쓰는 C++ 클래스. Script
//     (Scene/Script.h) 를 상속하고 SHERLOCK_SCRIPT 로 등록함 — 유니티의 MonoBehaviour 에 해당. 사용법은
//     Script.h 의 주석 참고.
//
// 실행 시점: 에디터에서 ▶ Play 중일 때만 Scene::Update / FixedUpdate 가 컴포넌트를 호출함. 편집 중에는
// 아무것도 움직이지 않음. Start 는 재생 시작 후(또는 재생 중 추가된 뒤) 첫 프레임에 한 번 호출됨. ■ Stop 은
// 재생 전 씬 스냅샷으로 되돌리므로 재생 중 바뀐 내용은 남지 않음.
//
// 소유: 컴포넌트는 GameObject 가 unique_ptr 로 소유함. GameObject 는 Scene 이 unique_ptr 로 소유하므로 재생
// 중 벡터가 늘어나도 주소가 바뀌지 않고, 따라서 컴포넌트가 소유자 포인터를 들고 있어도 안전함 (11-C 에서 값
// 벡터를 unique_ptr 벡터로 바꾼 이유).

// 재생 컨텍스트: 컴포넌트가 사용하는 엔진 시스템 모음. Scene 이 하나를 들고 있다가 재생 시작 때 채움.
struct PlayContext
{
	Scene* scene = nullptr;
	Input* input = nullptr;
	Camera* camera = nullptr;
	float totalTime = 0.0f;      // 재생 시작 뒤 누적 시간 (시간 배율 반영)
	bool cameraDriven = false;   // 이번 프레임에 어떤 컴포넌트가 카메라를 움직였음 → 앱의 에디터 카메라 컨트롤러는 카메라를 건드리지 않음
	UISystem* ui = nullptr;      // 게임 UI (포인터가 UI 위인가, 선택한 요소). 재생 중 Scene::Update 첫머리에 이벤트를 처리함
};

// 속성 방문자. 컴포넌트는 Reflect 에서 자기 필드를 열거할 뿐, 방문자가 그 필드로 무엇을 하는지는 모름:
// SceneSerializer 의 방문자는 JSON 을 쓰거나 읽고, Editor 의 방문자는 ImGui 위젯을 그림 (Scene 계층은 ImGui 를 모름).
class PropertyVisitor
{
public:
	virtual ~PropertyVisitor() = default;
	virtual void Float(const char* name, float& value, float speed = 0.1f) = 0;
	virtual void Int(const char* name, int& value) = 0;
	virtual void Bool(const char* name, bool& value) = 0;
	virtual void Float3(const char* name, DirectX::XMFLOAT3& value, float speed = 0.1f) = 0;
	virtual void String(const char* name, std::string& value) = 0;

	// ---- 게임 UI 가 쓰는 종류 (docs/plan/ui-canvas-plan.html 1단계) ----
	// 끝에 붙임: 이미 빌드된 스크립트 DLL 이 위 함수를 부르는 vtable 순서가 그대로임. 기본 구현은 위의 기본 종류로
	// 풀어 씀 — 새 방문자를 만들 때 다 구현하지 않아도 값이 사라지지 않음.
	enum class AssetKind : int
	{
		Texture,
		Font,
	};
	virtual void Float2(const char* name, DirectX::XMFLOAT2& value, float speed = 0.1f)
	{
		DirectX::XMFLOAT3 v(value.x, value.y, 0.0f);
		Float3(name, v, speed);
		value = DirectX::XMFLOAT2(v.x, v.y);
	}
	// 색: rgba 0~1, sRGB 값 그대로 (UI 는 감마 공간에서 섞음)
	virtual void Color(const char* name, DirectX::XMFLOAT4& value)
	{
		DirectX::XMFLOAT3 rgb(value.x, value.y, value.z);
		Float3(name, rgb, 0.01f);
		value = DirectX::XMFLOAT4(rgb.x, rgb.y, rgb.z, value.w);
	}
	// 정수 하나를 이름 목록 중 하나로 고름. 저장은 정수 그대로.
	virtual void Enum(const char* name, int& value, const char* const* names, int count)
	{
		(void)names;
		(void)count;
		Int(name, value);
	}
	// 에셋 경로 (TextureCache::Load2D 이름 규칙: "builtin:*", "asset:<Assets 기준>", 또는 Assets/Textures 기준)
	virtual void Asset(const char* name, std::string& value, AssetKind kind)
	{
		(void)kind;
		String(name, value);
	}
	// 다른 오브젝트 참조. 소유 오브젝트 기준 상대 경로 ("Fill", "Area/Fill", "..", "../Handle"). 빈 문자열 = 없음.
	virtual void ObjectRef(const char* name, std::string& value)
	{
		String(name, value);
	}
	// 여러 줄 문자열 (UIText.text). 저장은 String 과 같음, Inspector 만 여러 줄 입력칸
	virtual void MultilineString(const char* name, std::string& value)
	{
		String(name, value);
	}
	// UI 이벤트의 저장되는 연결 목록 (Button.onClick 등). 기본 구현은 JSON 배열 텍스트 문자열 하나로 풀어 씀
	virtual void Event(const char* name, UIEventBase& value)
	{
		std::string text = UIEventEncode(value);
		const std::string before = text;
		String(name, text);
		if (text != before)
		{
			UIEventDecode(value, text);
		}
	}
};

class SHERLOCK_API Behaviour
{
public:
	virtual ~Behaviour() = default;

	virtual const char* GetTypeName() const = 0;
	virtual void Reflect(PropertyVisitor& visitor) { (void)visitor; }
	virtual void Start() {}
	virtual void Update(float dt) { (void)dt; }
	virtual void FixedUpdate(float fixedDt) { (void)fixedDt; }

	GameObject& GetOwner() { return *m_owner; }
	const GameObject& GetOwner() const { return *m_owner; }
	Transform& GetTransform();
	PlayContext& GetContext() { return *m_context; }   // Start/Update 안에서만 유효
	bool HasContext() const { return m_context != nullptr; }

	bool enabled = true;   // 꺼진 컴포넌트는 Update/FixedUpdate 가 호출되지 않음 (Start 는 호출됨)

	// ---- Scene / GameObject 가 호출함 ----
	void Attach(GameObject* owner) { m_owner = owner; }
	void BeginPlay(PlayContext* context)
	{
		m_context = context;
		m_started = false;
	}
	void EndPlay()
	{
		m_context = nullptr;
		m_started = false;
	}
	bool HasStarted() const { return m_started; }
	void MarkStarted() { m_started = true; }

private:
	GameObject* m_owner = nullptr;
	PlayContext* m_context = nullptr;
	bool m_started = false;
};

// 이름 → 생성 함수 대응표. 매크로가 정적 초기화 시점에 등록함.
// 엔진 컴포넌트(Rigidbody·FollowTarget·CameraComponent)는 SherlockEngine.dll 이 올라올 때, 프로젝트 스크립트는
// <프로젝트>Scripts.dll 을 LoadLibrary 할 때 등록됨. 표는 엔진 DLL 안에 한 벌뿐이라 스크립트 DLL 의 등록도
// 같은 표로 들어감.
namespace BehaviourRegistry
{
	using Factory = std::function<std::unique_ptr<Behaviour>()>;
	SHERLOCK_API void Register(const char* typeName, Factory factory, bool isScript);   // 같은 이름이 이미 있으면 무시 (경고)
	// 스크립트 DLL 을 내리기 전에 그 DLL 이 등록한 이름을 지움 (ScriptModule::Unload). 생성 함수가 DLL 코드를
	// 가리키기 때문.
	SHERLOCK_API void Unregister(const std::string& typeName);
	SHERLOCK_API std::unique_ptr<Behaviour> Create(const std::string& typeName);   // 모르는 이름이면 nullptr (+ 경고 로그)
	SHERLOCK_API const std::vector<std::string>& GetTypeNames();                   // 정렬된 목록 (Inspector 의 Add Component)
	SHERLOCK_API bool IsScript(const std::string& typeName);                       // SHERLOCK_SCRIPT 로 등록된 타입인지 여부
}

#define SHERLOCK_REGISTER_BEHAVIOUR_(ClassName, IsScript) \
	const char* ClassName::GetTypeName() const \
	{ \
		return #ClassName; \
	} \
	namespace \
	{ \
		struct ClassName##_Registrar \
		{ \
			ClassName##_Registrar() \
			{ \
				BehaviourRegistry::Register(#ClassName, []() -> std::unique_ptr<Behaviour> { \
					return std::make_unique<ClassName>(); \
				}, IsScript); \
			} \
		} s_##ClassName##_Registrar; \
	}

#define SHERLOCK_BEHAVIOUR(ClassName) SHERLOCK_REGISTER_BEHAVIOUR_(ClassName, false)   // 엔진 컴포넌트
#define SHERLOCK_SCRIPT(ClassName) SHERLOCK_REGISTER_BEHAVIOUR_(ClassName, true)       // 사용자 스크립트 (Script 상속)

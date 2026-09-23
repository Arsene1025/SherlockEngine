#pragma once
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

// 11-C단계: 컴포넌트 기반 클래스. 유니티의 Component 에 해당한다.
//
// 두 종류가 이것을 상속한다:
//   - 엔진 컴포넌트 (Game/Components/, Scene/): Rigidbody, CameraComponent, FollowTarget — SHERLOCK_BEHAVIOUR 로 등록
//   - 스크립트 (Game/Scripts/): 사용자가 오브젝트의 움직임·규칙을 직접 쓰는 C++ 클래스. Script (Scene/Script.h) 를 상속하고
//     SHERLOCK_SCRIPT 로 등록한다 — 유니티의 MonoBehaviour. 사용법은 Script.h 의 주석.
//
// 언제 도는가: 에디터의 ▶ Play 동안만 Scene::Update / FixedUpdate 가 컴포넌트를 부른다. 편집 중에는 아무것도 움직이지 않는다.
// Start 는 재생 시작(또는 재생 중 추가된 뒤) 첫 프레임에 한 번. ■ Stop 은 재생 전 씬 스냅샷을 되돌리므로 재생 중 바뀐 것은 남지 않는다.
//
// 소유: GameObject 가 unique_ptr 로 갖는다. GameObject 는 Scene 이 unique_ptr 로 갖고 재생 중 벡터가 늘어도 주소가 바뀌지 않으므로
// 컴포넌트가 소유자 포인터를 들어도 안전하다 (11-C 에서 값 벡터에서 바꾼 이유).

// 재생 컨텍스트: 컴포넌트가 보는 엔진 시스템. Scene 이 하나 들고 재생 시작 때 채운다.
struct PlayContext
{
	Scene* scene = nullptr;
	Input* input = nullptr;
	Camera* camera = nullptr;
	float totalTime = 0.0f;       // 재생 시작 뒤 누적 시간 (시간 배율 반영)
	bool cameraDriven = false;    // 이번 프레임에 어떤 컴포넌트가 카메라를 움직였다 → 앱의 에디터 카메라 컨트롤러는 손대지 않는다
};

// 속성 방문자. 컴포넌트는 Reflect 에서 자기 필드를 열거하고, 방문자가 무엇을 하는지는 모른다:
// SceneSerializer 의 방문자는 JSON 을 쓰고/읽고, Editor 의 방문자는 ImGui 위젯을 그린다 (Scene 계층은 ImGui 를 모른다).
class PropertyVisitor
{
public:
	virtual ~PropertyVisitor() = default;
	virtual void Float(const char* name, float& value, float speed = 0.1f) = 0;
	virtual void Int(const char* name, int& value) = 0;
	virtual void Bool(const char* name, bool& value) = 0;
	virtual void Float3(const char* name, DirectX::XMFLOAT3& value, float speed = 0.1f) = 0;
	virtual void String(const char* name, std::string& value) = 0;
};

class Behaviour
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

	bool enabled = true;   // 꺼진 컴포넌트는 Update/FixedUpdate 를 받지 않는다 (Start 는 받는다)

	// ---- Scene / GameObject 가 부른다 ----
	void Attach(GameObject* owner) { m_owner = owner; }
	void BeginPlay(PlayContext* context) { m_context = context; m_started = false; }
	void EndPlay() { m_context = nullptr; m_started = false; }
	bool HasStarted() const { return m_started; }
	void MarkStarted() { m_started = true; }

private:
	GameObject* m_owner = nullptr;
	PlayContext* m_context = nullptr;
	bool m_started = false;
};

// 이름 → 생성 함수. 매크로가 정적 초기화로 등록한다 (모든 컴포넌트가 exe 에 직접 컴파일되므로
// 정적 라이브러리의 "참조되지 않은 .obj 탈락" 문제가 없다).
namespace BehaviourRegistry
{
	using Factory = std::function<std::unique_ptr<Behaviour>()>;
	void Register(const char* typeName, Factory factory, bool isScript);
	std::unique_ptr<Behaviour> Create(const std::string& typeName);   // 모르는 이름이면 nullptr (+ 경고 로그)
	const std::vector<std::string>& GetTypeNames();                    // 정렬된 목록 (Inspector 의 Add Component)
	bool IsScript(const std::string& typeName);                        // SHERLOCK_SCRIPT 로 등록된 것
}

#define SHERLOCK_REGISTER_BEHAVIOUR_(ClassName, IsScript) \
	const char* ClassName::GetTypeName() const { return #ClassName; } \
	namespace { struct ClassName##_Registrar { ClassName##_Registrar() { \
		BehaviourRegistry::Register(#ClassName, []() -> std::unique_ptr<Behaviour> { return std::make_unique<ClassName>(); }, IsScript); } } \
		s_##ClassName##_Registrar; }

#define SHERLOCK_BEHAVIOUR(ClassName) SHERLOCK_REGISTER_BEHAVIOUR_(ClassName, false)   // 엔진 컴포넌트
#define SHERLOCK_SCRIPT(ClassName) SHERLOCK_REGISTER_BEHAVIOUR_(ClassName, true)       // 사용자 스크립트 (Script 상속)

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

// 11-C단계: 모든 컴포넌트의 기반 클래스. 유니티의 Component 에 해당함.
//
// 다음 두 종류가 이 클래스를 상속함:
//   - 엔진 컴포넌트 (Game/Components/, Scene/): Rigidbody, CameraComponent, FollowTarget — SHERLOCK_BEHAVIOUR 로 등록
//   - 스크립트 (<프로젝트>\Scripts\): 사용자가 오브젝트의 움직임·규칙을 직접 쓰는 C++ 클래스. Script (Scene/Script.h) 를 상속하고
//     SHERLOCK_SCRIPT 로 등록함 — 유니티의 MonoBehaviour 에 해당. 사용법은 Script.h 의 주석 참고.
//
// 실행 시점: 에디터에서 ▶ Play 중일 때만 Scene::Update / FixedUpdate 가 컴포넌트를 호출함. 편집 중에는 아무것도 움직이지 않음.
// Start 는 재생 시작 후(또는 재생 중 추가된 뒤) 첫 프레임에 한 번 호출됨. ■ Stop 은 재생 전 씬 스냅샷으로 되돌리므로 재생 중 바뀐 내용은 남지 않음.
//
// 소유: 컴포넌트는 GameObject 가 unique_ptr 로 소유함. GameObject 는 Scene 이 unique_ptr 로 소유하므로 재생 중 벡터가 늘어나도 주소가 바뀌지 않고,
// 따라서 컴포넌트가 소유자 포인터를 들고 있어도 안전함 (11-C 에서 값 벡터를 unique_ptr 벡터로 바꾼 이유).

// 재생 컨텍스트: 컴포넌트가 사용하는 엔진 시스템 모음. Scene 이 하나를 들고 있다가 재생 시작 때 채움.
struct PlayContext
{
	Scene* scene = nullptr;
	Input* input = nullptr;
	Camera* camera = nullptr;
	float totalTime = 0.0f;       // 재생 시작 뒤 누적 시간 (시간 배율 반영)
	bool cameraDriven = false;    // 이번 프레임에 어떤 컴포넌트가 카메라를 움직였음 → 앱의 에디터 카메라 컨트롤러는 카메라를 건드리지 않음
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

	bool enabled = true;   // 꺼진 컴포넌트는 Update/FixedUpdate 가 호출되지 않음 (Start 는 호출됨)

	// ---- Scene / GameObject 가 호출함 ----
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

// 이름 → 생성 함수 대응표. 매크로가 정적 초기화 시점에 등록함 (모든 컴포넌트가 exe 에 직접 컴파일되므로
// 정적 라이브러리의 "참조되지 않은 .obj 탈락" 문제가 없음).
namespace BehaviourRegistry
{
	using Factory = std::function<std::unique_ptr<Behaviour>()>;
	void Register(const char* typeName, Factory factory, bool isScript);
	std::unique_ptr<Behaviour> Create(const std::string& typeName);   // 모르는 이름이면 nullptr (+ 경고 로그)
	const std::vector<std::string>& GetTypeNames();                    // 정렬된 목록 (Inspector 의 Add Component)
	bool IsScript(const std::string& typeName);                        // SHERLOCK_SCRIPT 로 등록된 타입인지 여부
}

#define SHERLOCK_REGISTER_BEHAVIOUR_(ClassName, IsScript) \
	const char* ClassName::GetTypeName() const { return #ClassName; } \
	namespace { struct ClassName##_Registrar { ClassName##_Registrar() { \
		BehaviourRegistry::Register(#ClassName, []() -> std::unique_ptr<Behaviour> { return std::make_unique<ClassName>(); }, IsScript); } } \
		s_##ClassName##_Registrar; }

#define SHERLOCK_BEHAVIOUR(ClassName) SHERLOCK_REGISTER_BEHAVIOUR_(ClassName, false)   // 엔진 컴포넌트
#define SHERLOCK_SCRIPT(ClassName) SHERLOCK_REGISTER_BEHAVIOUR_(ClassName, true)       // 사용자 스크립트 (Script 상속)

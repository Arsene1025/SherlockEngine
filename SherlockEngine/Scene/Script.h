#pragma once
#include "Core/EngineApi.h"   // 2026-10-08: SHERLOCK_API (엔진 DLL 내보내기)
#include "Scene/Behaviour.h"
#include "Scene/GameObject.h"
#include "Core/Input.h"   // 스크립트가 GetInput() 만으로 키를 읽을 수 있게 함 (별도 include 불필요)

// 11-C단계: 모든 스크립트의 기반 클래스 — 유니티의 MonoBehaviour 에 해당함.
//
// 오브젝트의 움직임·규칙은 이 클래스를 상속한 C++ 클래스에 직접 작성함. 스크립트 하나는 언리얼처럼 <프로젝트>\Scripts\<이름>.h (선언) + .cpp (구현 +
// SHERLOCK_SCRIPT) 한 쌍으로 이뤄짐 — Inspector 의 "New Script..." 가 템플릿으로 두 파일을 만들어 줌 (Scripts\**\*.cpp 는 와일드카드로 <프로젝트>Scripts.dll 에 컴파일됨). 빌드하면 에디터가 DLL 을 다시 올려(2026-10-08 B안)
// Inspector 의 "Add Component" 스크립트 목록에 나타나고, 씬 파일에는 이름으로 저장됨. Reflect 에 열거한 필드는 Inspector 에서 편집되고
// 씬에 저장됨 ([SerializeField] 에 해당). 헤더가 따로 있으므로 다른 스크립트가 이를 #include 한 뒤 Find("이름")->GetBehaviour<이름>() 로 가져올 수 있음.
//
//   // Bouncer.h
//   #pragma once
//   #include "Scene/Script.h"
//   class Bouncer : public Script
//   {
//   public:
//       const char* GetTypeName() const override;                       // SHERLOCK_SCRIPT 가 정의 (.cpp)
//       void Reflect(PropertyVisitor& v) override { v.Float("height", height); v.Float("speed", speed); }
//       void Start() override;
//       void Update(float dt) override;
//       float height = 1.5f, speed = 2.0f;
//   private:
//       DirectX::XMFLOAT3 m_base{}; float m_time = 0.0f;
//   };
//
//   // Bouncer.cpp
//   #include "pch.h"
//   #include "Bouncer.h"
//   void Bouncer::Start() { m_base = GetPosition(); }
//   void Bouncer::Update(float dt)
//   {
//       m_time += dt;
//       SetPosition(m_base.x, m_base.y + height * sinf(speed * m_time), m_base.z);   // 또는 Translate / Rotate / LookAt
//       if (GetInput().IsKeyPressed(VK_SPACE)) Rotate(0.0f, 0.5f, 0.0f);
//   }
//   SHERLOCK_SCRIPT(Bouncer)
//
// 생명주기: Start(재생 시작 뒤 첫 프레임) → Update(dt, 매 프레임, 시간 배율 반영) → FixedUpdate(고정 스텝, 물리).
// 아래 헬퍼는 Start/Update/FixedUpdate 안에서만 사용함 (재생 컨텍스트가 그때만 존재함).
// 다른 오브젝트/스크립트의 포인터는 Start 에서 찾아 멤버에 저장해 둠. Stop 은 씬을 스냅샷에서 다시 만들므로 재생할 때마다 다시 찾아야 함.
class SHERLOCK_API Script : public Behaviour
{
public:
	// ---- 자기 오브젝트 ----
	GameObject& GetGameObject() { return GetOwner(); }
	const std::string& GetName() const { return GetOwner().GetName(); }
	template <std::derived_from<Behaviour> T> T* GetComponent() { return GetOwner().GetBehaviour<T>(); }   // 같은 오브젝트의 다른 컴포넌트 (예: Rigidbody). C++20 콘셉트로 T 를 Behaviour 파생으로 제한

	// ---- Transform 단축 함수 (Transform 의 같은 이름 함수를 호출함). 모두 부모 기준 로컬 값 (유니티의 localPosition 등) ----
	const DirectX::XMFLOAT3& GetPosition() const { return GetOwner().GetTransform().GetPosition(); }
	const DirectX::XMFLOAT3& GetRotation() const { return GetOwner().GetTransform().GetRotation(); }
	void SetPosition(float x, float y, float z) { GetTransform().SetPosition(x, y, z); }
	void SetPosition(const DirectX::XMFLOAT3& p) { GetTransform().SetPosition(p); }
	void Translate(float x, float y, float z) { GetTransform().Translate(x, y, z); }
	void TranslateLocal(float forward, float right, float up) { GetTransform().TranslateLocal(forward, right, up); }
	void Rotate(float pitch, float yaw, float roll) { GetTransform().Rotate(pitch, yaw, roll); }
	void LookAt(const DirectX::XMFLOAT3& target) { GetTransform().LookAt(target); }
	void SetScale(float s) { GetTransform().SetScale(s, s, s); }

	// ---- 11-F단계: 계층. 부모가 있는 오브젝트의 월드 좌표가 필요할 때 (유니티의 transform.position / SetParent) ----
	GameObject* GetParent() { return GetOwner().GetParent(); }
	bool SetParent(GameObject* parent, bool keepWorld = true) { return GetOwner().SetParent(parent, keepWorld); }
	DirectX::XMFLOAT3 GetWorldPosition() const { return GetOwner().GetWorldPosition(); }
	void SetWorldPosition(const DirectX::XMFLOAT3& p) { GetOwner().SetWorldPosition(p); }

	// ---- 엔진 시스템 (재생 중) ----
	Input& GetInput();                        // 키보드·마우스. VK_LEFT, 'A', VK_SPACE …
	Scene& GetScene();
	float GetTime() { return GetContext().totalTime; }   // 재생 시작 뒤 누적 초
	GameObject* Find(const std::string& name);           // 이름으로 다른 오브젝트를 찾음. 없으면 nullptr

	// ---- 2026-10-15: 생성과 지연 파괴 (유니티의 Instantiate / Destroy / FindObjectById) ----
	// Instantiate: 프리팹(Assets 기준 상대 경로, 예: "Prefabs\\Bullet.prefab")을 position(로컬, 부모 없는 루트)에 만들고 루트를 돌려줌. 실패하면 nullptr.
	//   같은 프리팹의 인스턴스는 메시·재질을 공유하므로 매 프레임 불러도 자원이 늘지 않음. 새 오브젝트의 컴포넌트는 같은 프레임 순회가 거기까지 오면 Start 와 첫 Update 를 받음.
	// Destroy: 오브젝트(와 자식)를 지우도록 표시함. 프레임 끝에 지워지고, 그때까지 남은 Update 는 받지 않음. 자기 자신도 지울 수 있음(DestroySelf) —
	//   호출 뒤 이 함수의 나머지 코드는 계속 실행되지만 멤버 포인터가 가리키는 다른 오브젝트가 같은 프레임에 지워질 수 있다는 점은 같음.
	// 오브젝트 포인터 규칙: Find 로 얻은 GameObject* 를 멤버에 오래 들고 있으면 그 오브젝트가 Destroy 됐을 때 dangling 이 됨.
	//   오래 들고 있을 것은 GetId() 를 저장하고, 쓸 때마다 FindById(id) 로 다시 찾음 (nullptr 이면 이미 지워진 것). Stop 이후에도 ID 는 같은 오브젝트를 가리킴.
	GameObject* Instantiate(const std::string& prefabPath, const DirectX::XMFLOAT3& position);
	GameObject* Instantiate(const GameObject& source);   // 씬의 오브젝트를 복제 (메시·재질 공유, 이름 뒤에 " (Clone)")
	void Destroy(GameObject* object);
	void DestroySelf() { Destroy(&GetOwner()); }
	GameObject* FindById(uint64_t id);
};

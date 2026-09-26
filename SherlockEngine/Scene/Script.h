#pragma once
#include "Scene/Behaviour.h"
#include "Scene/GameObject.h"
#include "Core/Input.h"   // 스크립트가 GetInput() 만으로 키를 읽을 수 있게 함 (별도 include 불필요)

// 11-C단계: 모든 스크립트의 기반 클래스 — 유니티의 MonoBehaviour 에 해당함.
//
// 오브젝트의 움직임·규칙은 이 클래스를 상속한 C++ 클래스에 직접 작성함. 스크립트 하나는 언리얼처럼 <프로젝트>\Scripts\<이름>.h (선언) + .cpp (구현 +
// SHERLOCK_SCRIPT) 한 쌍으로 이뤄짐 — Inspector 의 "New Script..." 가 템플릿으로 두 파일을 만들어 줌 (Scripts\*.cpp 는 와일드카드로 컴파일됨). 빌드 후 재실행하면
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
class Script : public Behaviour
{
public:
	// ---- 자기 오브젝트 ----
	GameObject& GetGameObject() { return GetOwner(); }
	const std::string& GetName() const { return GetOwner().GetName(); }
	template <typename T> T* GetComponent() { return GetOwner().GetBehaviour<T>(); }   // 같은 오브젝트의 다른 컴포넌트 (예: Rigidbody)

	// ---- Transform 단축 함수 (Transform 의 같은 이름 함수를 호출함) ----
	const DirectX::XMFLOAT3& GetPosition() const { return GetOwner().GetTransform().GetPosition(); }
	const DirectX::XMFLOAT3& GetRotation() const { return GetOwner().GetTransform().GetRotation(); }
	void SetPosition(float x, float y, float z) { GetTransform().SetPosition(x, y, z); }
	void SetPosition(const DirectX::XMFLOAT3& p) { GetTransform().SetPosition(p); }
	void Translate(float x, float y, float z) { GetTransform().Translate(x, y, z); }
	void TranslateLocal(float forward, float right, float up) { GetTransform().TranslateLocal(forward, right, up); }
	void Rotate(float pitch, float yaw, float roll) { GetTransform().Rotate(pitch, yaw, roll); }
	void LookAt(const DirectX::XMFLOAT3& target) { GetTransform().LookAt(target); }
	void SetScale(float s) { GetTransform().SetScale(s, s, s); }

	// ---- 엔진 시스템 (재생 중) ----
	Input& GetInput();                        // 키보드·마우스. VK_LEFT, 'A', VK_SPACE …
	Scene& GetScene();
	float GetTime() { return GetContext().totalTime; }   // 재생 시작 뒤 누적 초
	GameObject* Find(const std::string& name);           // 이름으로 다른 오브젝트를 찾음. 없으면 nullptr
};

#pragma once
#include <concepts>   // C++20: std::derived_from
#include <memory>
#include <string>
#include <vector>
#include "Scene/Transform.h"
#include "Scene/Behaviour.h"

class Mesh;
struct Material;

// 씬의 물체 하나. Transform 값 하나, 그릴 메시·재질에 대한 참조, 11-C단계에서 추가된 컴포넌트(Behaviour) 목록을 가짐.
//
// GPU 자원은 갖지 않음. 메시 → 정점/인덱스 버퍼, 재질 → 상수버퍼/ResourceSet 대응은
// Renderer의 캐시가 관리함. (rendering-analysis D3: Scene은 CPU 데이터, GPU 자원은
// Renderer 측 캐시) 메시와 재질은 Scene이 unique_ptr로 소유하므로 포인터가 바뀌지 않음.
//
// 9단계: 메시가 서브메시 여러 개(재질 슬롯)를 가지면 슬롯마다 재질을 둠. 슬롯에 재질이 없으면
// 오브젝트의 기본 재질(material)을 쓰고, 그것도 없으면 Renderer 의 기본 재질을 씀.
//
// 11-C단계: Scene 이 GameObject 도 unique_ptr 로 소유함 (이전에는 값 벡터였음). 컴포넌트가 소유자 포인터를 들고 있기 때문.
//
// 11-F단계: 부모-자식 계층. Transform 은 부모 기준 로컬 값이고, 월드 행렬 = local × parent.world 를 GetWorldMatrix 가 재귀로 만듦.
// 소유는 여전히 Scene 의 평면 벡터가 하고(인덱스 선택·순회는 그대로), 부모/자식 포인터는 비소유 링크임 — 주소가 안정적인 것은
// 11-C 의 unique_ptr 전환 덕분. 부모를 지우면 자식도 함께 지워짐(Scene::RemoveObject, 유니티와 같음).
class GameObject
{
public:
	GameObject() = default;
	GameObject(const Mesh* mesh, const Material* material, const char* name)
		: mesh(mesh), material(material), name(name ? name : "") {}
	GameObject(const GameObject&) = delete;
	GameObject& operator=(const GameObject&) = delete;

	Transform& GetTransform() { return transform; }
	const Transform& GetTransform() const { return transform; }

	const Mesh* GetMesh() const { return mesh; }
	void SetMesh(const Mesh* value) { mesh = value; }

	const Material* GetMaterial() const { return material; }   // nullptr이면 Renderer의 기본 재질
	void SetMaterial(const Material* value) { material = value; }

	// 서브메시 재질 슬롯. 비어 있으면 모든 서브메시가 material 을 씀.
	void SetSlotMaterials(std::vector<const Material*>&& slots) { slotMaterials = std::move(slots); }
	const std::vector<const Material*>& GetSlotMaterials() const { return slotMaterials; }
	const Material* GetMaterialForSlot(uint32_t slot) const
	{
		if (slot < slotMaterials.size() && slotMaterials[slot] != nullptr) return slotMaterials[slot];
		return material;
	}

	const std::string& GetName() const { return name; }
	void SetName(const std::string& value) { name = value; }   // 11-B단계: 배치한 모델에 번호가 붙은 이름을 줄 때 사용

	// ---- 11-F단계: 계층 ----
	GameObject* GetParent() { return parent; }
	const GameObject* GetParent() const { return parent; }
	const std::vector<GameObject*>& GetChildren() const { return children; }
	bool IsDescendantOf(const GameObject* ancestor) const;   // 자기 자신은 포함하지 않음
	// 부모를 바꿈. nullptr 이면 루트로. keepWorld 가 참이면 월드 자세가 유지되도록 로컬 값을 다시 계산함
	// (유니티 SetParent(parent, worldPositionStays=true)). 자기 자신이나 자기 자손을 부모로 지정하면 false 를 돌려주고 아무것도 하지 않음.
	bool SetParent(GameObject* newParent, bool keepWorld = true);

	// 월드 행렬 = local × parent.world (재귀). 부모가 없으면 로컬 행렬 그대로. 렌더러·기즈모·픽킹·카메라가 씀.
	DirectX::XMMATRIX GetWorldMatrix() const;
	DirectX::XMMATRIX GetParentWorldMatrix() const;   // 부모가 없으면 단위행렬
	DirectX::XMFLOAT3 GetWorldPosition() const;
	// 월드 기준 자세 지정: 부모 월드의 역행렬을 곱해 로컬로 바꿔 저장함. 부모 스케일이 비균등하면 분해가 근사가 됨.
	bool SetWorldMatrix(const DirectX::XMMATRIX& world);
	void SetWorldPosition(const DirectX::XMFLOAT3& position);
	bool SetWorldRotation(const DirectX::XMMATRIX& rotation);   // 순수 회전 행렬. 위치·스케일은 유지

	// ---- 11-C단계: 컴포넌트 ----
	Behaviour& AddBehaviour(std::unique_ptr<Behaviour> behaviour)
	{
		behaviour->Attach(this);
		behaviours.push_back(std::move(behaviour));
		return *behaviours.back();
	}
	Behaviour* AddBehaviour(const std::string& typeName)   // 레지스트리에 등록된 이름으로 추가함. 모르는 이름이면 nullptr
	{
		std::unique_ptr<Behaviour> created = BehaviourRegistry::Create(typeName);
		if (!created) return nullptr;
		return &AddBehaviour(std::move(created));
	}
	void RemoveBehaviour(size_t index) { if (index < behaviours.size()) behaviours.erase(behaviours.begin() + index); }
	std::vector<std::unique_ptr<Behaviour>>& GetBehaviours() { return behaviours; }
	const std::vector<std::unique_ptr<Behaviour>>& GetBehaviours() const { return behaviours; }
	// C++20 (2026-09-28): T 는 Behaviour 파생 클래스여야 함 (std::derived_from 콘셉트). 이전에는 무엇이든 받아
	// dynamic_cast 가 항상 nullptr 를 돌려주는 조용한 실수가 가능했음. 지금은 GetBehaviour<Transform>() 같은 호출이 컴파일되지 않음.
	template <std::derived_from<Behaviour> T> T* GetBehaviour()
	{
		for (auto& b : behaviours) if (T* typed = dynamic_cast<T*>(b.get())) return typed;
		return nullptr;
	}

	// ---- Scene 이 호출함 ----
	// 계층에서 떼어냄: 부모의 children 에서 빠지고 자식들의 parent 를 끊음 (자식은 루트가 됨). 소멸자에서는 하지 않음 —
	// Scene::Clear 가 벡터를 통째로 지울 때 이미 죽은 부모를 건드리게 되기 때문. RemoveObject 가 지우기 직전에 호출함.
	void DetachFromHierarchy();

private:
	Transform transform;
	const Mesh* mesh = nullptr;
	const Material* material = nullptr;
	std::vector<const Material*> slotMaterials;
	std::string name;
	std::vector<std::unique_ptr<Behaviour>> behaviours;
	GameObject* parent = nullptr;          // 11-F단계: 비소유. 소유는 Scene 의 벡터
	std::vector<GameObject*> children;     // 11-F단계: 비소유. Hierarchy 표시 순서
};

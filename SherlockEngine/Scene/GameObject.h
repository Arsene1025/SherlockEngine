#pragma once
#include <memory>
#include <string>
#include <vector>
#include "Scene/Transform.h"
#include "Scene/Behaviour.h"

class Mesh;
struct Material;

// 씬의 물체 하나. Transform 값 하나와 그릴 메시·재질에 대한 참조, 그리고 11-C단계의 컴포넌트(Behaviour) 목록.
//
// GPU 자원은 갖지 않는다. 메시 → 정점/인덱스 버퍼, 재질 → 상수버퍼/ResourceSet 대응은
// Renderer의 캐시가 관리한다. (rendering-analysis D3: Scene은 CPU 데이터, GPU 자원은
// Renderer 측 캐시) 메시와 재질은 Scene이 unique_ptr로 소유하므로 포인터가 안정적이다.
//
// 9단계: 메시가 서브메시 여러 개(재질 슬롯)를 가지면 슬롯마다 재질을 둔다. 슬롯에 재질이 없으면
// 오브젝트의 기본 재질(material), 그것도 없으면 Renderer 의 기본 재질.
//
// 11-C단계: Scene 이 GameObject 도 unique_ptr 로 갖는다 (값 벡터였다). 컴포넌트가 소유자 포인터를 들기 때문.
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

	// 서브메시 재질 슬롯. 비어 있으면 모든 서브메시가 material 을 쓴다.
	void SetSlotMaterials(std::vector<const Material*>&& slots) { slotMaterials = std::move(slots); }
	const std::vector<const Material*>& GetSlotMaterials() const { return slotMaterials; }
	const Material* GetMaterialForSlot(uint32_t slot) const
	{
		if (slot < slotMaterials.size() && slotMaterials[slot] != nullptr) return slotMaterials[slot];
		return material;
	}

	const std::string& GetName() const { return name; }
	void SetName(const std::string& value) { name = value; }   // 11-B단계: 배치한 모델의 번호 이름

	// ---- 11-C단계: 컴포넌트 ----
	Behaviour& AddBehaviour(std::unique_ptr<Behaviour> behaviour)
	{
		behaviour->Attach(this);
		behaviours.push_back(std::move(behaviour));
		return *behaviours.back();
	}
	Behaviour* AddBehaviour(const std::string& typeName)   // 레지스트리 이름으로. 모르는 이름이면 nullptr
	{
		std::unique_ptr<Behaviour> created = BehaviourRegistry::Create(typeName);
		if (!created) return nullptr;
		return &AddBehaviour(std::move(created));
	}
	void RemoveBehaviour(size_t index) { if (index < behaviours.size()) behaviours.erase(behaviours.begin() + index); }
	std::vector<std::unique_ptr<Behaviour>>& GetBehaviours() { return behaviours; }
	const std::vector<std::unique_ptr<Behaviour>>& GetBehaviours() const { return behaviours; }
	template <typename T> T* GetBehaviour()
	{
		for (auto& b : behaviours) if (T* typed = dynamic_cast<T*>(b.get())) return typed;
		return nullptr;
	}

private:
	Transform transform;
	const Mesh* mesh = nullptr;
	const Material* material = nullptr;
	std::vector<const Material*> slotMaterials;
	std::string name;
	std::vector<std::unique_ptr<Behaviour>> behaviours;
};

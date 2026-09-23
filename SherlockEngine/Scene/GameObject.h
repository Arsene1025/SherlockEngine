#pragma once
#include <string>
#include <vector>
#include "Scene/Transform.h"

class Mesh;
struct Material;

// 씬의 물체 하나. Transform 값 하나와 그릴 메시·재질에 대한 참조.
//
// GPU 자원은 갖지 않는다. 메시 → 정점/인덱스 버퍼, 재질 → 상수버퍼/ResourceSet 대응은
// Renderer의 캐시가 관리한다. (rendering-analysis D3: Scene은 CPU 데이터, GPU 자원은
// Renderer 측 캐시) 메시와 재질은 Scene이 unique_ptr로 소유하므로 포인터가 안정적이다.
//
// 9단계: 메시가 서브메시 여러 개(재질 슬롯)를 가지면 슬롯마다 재질을 둔다. 슬롯에 재질이 없으면
// 오브젝트의 기본 재질(material), 그것도 없으면 Renderer 의 기본 재질.
class GameObject
{
public:
	GameObject() = default;
	GameObject(const Mesh* mesh, const Material* material, const char* name)
		: mesh(mesh), material(material), name(name ? name : "") {}

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

private:
	Transform transform;
	const Mesh* mesh = nullptr;
	const Material* material = nullptr;
	std::vector<const Material*> slotMaterials;
	std::string name;
};

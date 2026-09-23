#include "pch.h"
#include "Scene/Scene.h"
#include "Graphics/Mesh.h"   // unique_ptr<Mesh> 소멸에 완전한 타입 필요
#include "Graphics/Model.h"

using namespace DirectX;   // 이 파일 안에서만

Scene::Scene()
{
}

Scene::~Scene()
{
}

Mesh* Scene::AddMesh(Mesh&& mesh)
{
	return AddMesh(std::move(mesh), MeshSource{});
}

Mesh* Scene::AddMesh(Mesh&& mesh, const MeshSource& source)
{
	m_meshes.push_back(std::make_unique<Mesh>(std::move(mesh)));
	m_meshSources.push_back(source);
	return m_meshes.back().get();
}

const MeshSource* Scene::GetMeshSource(const Mesh* mesh) const
{
	for (size_t i = 0; i < m_meshes.size(); ++i)
	{
		if (m_meshes[i].get() == mesh) return i < m_meshSources.size() ? &m_meshSources[i] : nullptr;
	}
	return nullptr;
}

void Scene::AddModelImages(const Model& model)
{
	for (const ModelImage& image : model.images)
	{
		std::vector<uint8_t> bytes = image.encoded;
		AddImage(image.name, std::move(bytes));
	}
}

MeshSource MeshSource::Sphere(float radius, uint32_t slices, uint32_t stacks, const XMFLOAT4& color)
{
	MeshSource s; s.type = Type::Sphere; s.params[0] = radius; s.a = slices; s.b = stacks; s.color = color; return s;
}
MeshSource MeshSource::Cube(float size, const XMFLOAT4& color)
{
	MeshSource s; s.type = Type::Cube; s.params[0] = size; s.color = color; return s;
}
MeshSource MeshSource::Plane(float width, float depth, uint32_t m, uint32_t n, const XMFLOAT4& color)
{
	MeshSource s; s.type = Type::Plane; s.params[0] = width; s.params[1] = depth; s.a = m; s.b = n; s.color = color; return s;
}
MeshSource MeshSource::Cylinder(float bottom, float top, float height, uint32_t slices, uint32_t stacks, const XMFLOAT4& color)
{
	MeshSource s; s.type = Type::Cylinder; s.params[0] = bottom; s.params[1] = top; s.params[2] = height; s.a = slices; s.b = stacks; s.color = color; return s;
}
MeshSource MeshSource::FromModel(const std::wstring& path, uint32_t meshIndex)
{
	MeshSource s; s.type = Type::Model; s.modelPath = path; s.meshIndex = meshIndex; return s;
}
bool MeshSource::CreatePrimitive(Mesh& out) const
{
	switch (type)
	{
	case Type::Sphere:   out = Mesh::CreateSphere(params[0], a, b, color); return true;
	case Type::Cube:     out = Mesh::CreateCube(params[0], color); return true;
	case Type::Plane:    out = Mesh::CreatePlane(params[0], params[1], a, b, color); return true;
	case Type::Cylinder: out = Mesh::CreateCylinder(params[0], params[1], params[2], a, b, color); return true;
	default: return false;
	}
}

Material* Scene::AddMaterial(const Material& material)
{
	m_materials.push_back(std::make_unique<Material>(material));
	return m_materials.back().get();
}

GameObject& Scene::AddObject(const Mesh* mesh, const Material* material, const char* name)
{
	m_objects.emplace_back(mesh, material, name);
	return m_objects.back();
}

size_t Scene::AddModel(Model&& model, const Transform& transform)
{
	// 메시·재질을 옮기고 새 포인터를 기억한다. 인스턴스는 인덱스로 참조하므로 순서만 지키면 된다.
	std::vector<const Mesh*> meshes;
	meshes.reserve(model.meshes.size());
	for (size_t i = 0; i < model.meshes.size(); ++i)
	{
		meshes.push_back(AddMesh(std::move(model.meshes[i]), MeshSource::FromModel(model.sourcePath, static_cast<uint32_t>(i))));
	}
	std::vector<const Material*> materials;
	materials.reserve(model.materials.size());
	for (const Material& material : model.materials)
	{
		materials.push_back(AddMaterial(material));
	}
	for (ModelImage& image : model.images)
	{
		AddImage(image.name, std::move(image.encoded));
	}

	size_t created = 0;
	for (const ModelInstance& instance : model.instances)
	{
		if (instance.meshIndex >= meshes.size()) continue;
		GameObject& object = AddObject(meshes[instance.meshIndex], materials.empty() ? nullptr : materials.back(), instance.name.c_str());
		std::vector<const Material*> slots = materials;   // 슬롯 번호 = 모델 재질 인덱스
		object.SetSlotMaterials(std::move(slots));
		object.GetTransform() = transform;
		object.GetTransform().SetPreTransform(XMLoadFloat4x4(&instance.world));
		++created;
	}

	model.meshes.clear();
	model.materials.clear();
	model.images.clear();
	model.instances.clear();
	return created;
}

void Scene::Clear()
{
	m_objects.clear();
	m_lights.clear();
	m_images.clear();
	m_materials.clear();
	m_meshes.clear();
	m_meshSources.clear();
}

void Scene::AddImage(const std::string& name, std::vector<uint8_t>&& encoded)
{
	m_images[name] = std::move(encoded);
}

const std::vector<uint8_t>* Scene::FindImage(const std::string& name) const
{
	auto found = m_images.find(name);
	return found == m_images.end() ? nullptr : &found->second;
}

size_t Scene::AddModel(const Model& model, const Transform& transform)
{
	Model copy = model;
	return AddModel(std::move(copy), transform);
}

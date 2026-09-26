#include "pch.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Core/Log.h"
#include "Graphics/Mesh.h"   // unique_ptr<Mesh> 소멸에 완전한 타입 필요
#include "Graphics/Model.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	float WrapAngle(float a)
	{
		while (a > XM_PI) a -= XM_2PI;
		while (a < -XM_PI) a += XM_2PI;
		return a;
	}
}

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
	m_objects.push_back(std::make_unique<GameObject>(mesh, material, name));
	return *m_objects.back();
}

GameObject* Scene::FindObject(const std::string& name)
{
	for (auto& object : m_objects) if (object->GetName() == name) return object.get();
	return nullptr;
}

void Scene::RemoveObject(size_t index)
{
	if (index < m_objects.size()) m_objects.erase(m_objects.begin() + index);   // 메시·재질은 씬이 계속 소유함 (다른 오브젝트가 쓰고 있을 수 있음)
}

// ------------------------------------------------------------------ 11-C단계: 재생

void Scene::BeginPlay(Input* input, Camera* camera)
{
	m_playContext = PlayContext{};
	m_playContext.scene = this;
	m_playContext.input = input;
	m_playContext.camera = camera;
	m_playing = true;
	for (auto& object : m_objects)
		for (auto& behaviour : object->GetBehaviours()) behaviour->BeginPlay(&m_playContext);
}

void Scene::Update(float dt)
{
	if (!m_playing) return;
	m_playContext.totalTime += dt;
	m_playContext.cameraDriven = false;
	// 인덱스로 순회함: 컴포넌트가 재생 중 오브젝트를 추가해도(벡터 재할당) 안전함. 삭제는 순회 중에 하지 말 것.
	for (size_t i = 0; i < m_objects.size(); ++i)
	{
		auto& behaviours = m_objects[i]->GetBehaviours();
		for (size_t b = 0; b < behaviours.size(); ++b)
		{
			Behaviour& behaviour = *behaviours[b];
			if (!behaviour.HasStarted())
			{
				behaviour.BeginPlay(&m_playContext);   // 재생 중 추가된 컴포넌트
				behaviour.MarkStarted();
				behaviour.Start();
			}
			if (behaviour.enabled) behaviour.Update(dt);
		}
	}
	ApplyActiveCamera(dt);   // 모든 컴포넌트가 움직인 뒤 (FollowTarget 이 카메라 오브젝트를 옮긴 뒤) 카메라에 자세를 적용함
}

CameraComponent* Scene::FindActiveCamera()
{
	CameraComponent* best = nullptr;
	for (auto& object : m_objects)
	{
		for (auto& behaviour : object->GetBehaviours())
		{
			CameraComponent* camera = dynamic_cast<CameraComponent*>(behaviour.get());
			if (camera == nullptr || !camera->enabled) continue;
			if (best == nullptr || camera->priority > best->priority) best = camera;
		}
	}
	return best;
}

void Scene::ApplyActiveCamera(float dt)
{
	Camera* camera = m_playContext.camera;
	CameraComponent* next = FindActiveCamera();
	if (camera == nullptr || next == nullptr)
	{
		m_activeCamera = nullptr;   // 카메라 오브젝트가 없으면 에디터 카메라를 그대로 씀 (cameraDriven 도 false)
		return;
	}
	if (next != m_activeCamera)
	{
		// 카메라 전환. 첫 활성화(재생 시작)는 즉시 전환하고, 그 뒤에는 새 카메라의 blendTime 동안 현재 시점에서 보간함.
		m_blendFrom.position = camera->GetPosition();
		m_blendFrom.yaw = camera->GetYaw();
		m_blendFrom.pitch = camera->GetPitch();
		m_blendFrom.fovY = camera->GetFovY();
		m_blendDuration = m_activeCamera != nullptr ? (std::max)(0.0f, next->blendTime) : 0.0f;
		m_blendElapsed = 0.0f;
		m_activeCamera = next;
		Log::Info("활성 카메라: %s (priority %d%s)", next->GetOwner().GetName().c_str(), next->priority,
			m_blendDuration > 0.0f ? ", 블렌드" : "");
	}

	CameraPose pose = next->GetPose();
	if (m_blendDuration > 0.0f && m_blendElapsed < m_blendDuration)
	{
		m_blendElapsed += dt;
		const float x = (std::min)(1.0f, m_blendElapsed / m_blendDuration);
		const float t = x * x * (3.0f - 2.0f * x);   // smoothstep
		pose.position.x = m_blendFrom.position.x + (pose.position.x - m_blendFrom.position.x) * t;
		pose.position.y = m_blendFrom.position.y + (pose.position.y - m_blendFrom.position.y) * t;
		pose.position.z = m_blendFrom.position.z + (pose.position.z - m_blendFrom.position.z) * t;
		pose.yaw = m_blendFrom.yaw + WrapAngle(pose.yaw - m_blendFrom.yaw) * t;   // 짧은 쪽으로
		pose.pitch = m_blendFrom.pitch + (pose.pitch - m_blendFrom.pitch) * t;
		pose.fovY = m_blendFrom.fovY + (pose.fovY - m_blendFrom.fovY) * t;
	}
	camera->SetPosition(pose.position);
	camera->SetYawPitch(pose.yaw, pose.pitch);
	camera->SetLens(pose.fovY, camera->GetAspect(), next->nearZ, next->farZ);
	m_playContext.cameraDriven = true;
}

void Scene::FixedUpdate(float fixedDt)
{
	if (!m_playing) return;
	for (size_t i = 0; i < m_objects.size(); ++i)
	{
		auto& behaviours = m_objects[i]->GetBehaviours();
		for (size_t b = 0; b < behaviours.size(); ++b)
		{
			Behaviour& behaviour = *behaviours[b];
			if (behaviour.HasStarted() && behaviour.enabled) behaviour.FixedUpdate(fixedDt);   // Start 전에는 호출하지 않음 (Start 는 Update 에서 먼저 호출됨)
		}
	}
}

void Scene::EndPlay()
{
	m_playing = false;
	m_activeCamera = nullptr;
	for (auto& object : m_objects)
		for (auto& behaviour : object->GetBehaviours()) behaviour->EndPlay();
}

size_t Scene::AddModel(Model&& model, const Transform& transform)
{
	// 메시·재질을 옮기고 새 포인터를 기억함. 인스턴스는 인덱스로 참조하므로 순서만 지키면 됨.
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
	m_playing = false;
	m_activeCamera = nullptr;
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

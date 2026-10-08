#include "pch.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Core/Log.h"
#include "Graphics/Mesh.h"   // unique_ptr<Mesh> 소멸에 완전한 타입 필요
#include "Graphics/Model.h"
#include <algorithm>
#include <cmath>

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
	if (index >= m_objects.size()) return;
	// 11-F단계: 자식도 함께 지움 (유니티와 같음. 언리얼은 떼어 내지만, 여기서 계층은 "묶어서 옮기기" 용도라 함께 지우는 쪽이 자연스러움).
	// 서브트리를 먼저 모으고, 루트를 부모에서 떼어 낸 뒤, 벡터에서 해당 포인터들을 지움. 메시·재질은 씬이 계속 소유함 (다른 오브젝트가 쓰고 있을 수 있음).
	std::vector<GameObject*> doomed;
	const auto collect = [&doomed](GameObject* object, auto& self) -> void
	{
		doomed.push_back(object);
		for (GameObject* child : object->GetChildren()) self(child, self);
	};
	GameObject* root = m_objects[index].get();
	collect(root, collect);
	root->DetachFromHierarchy();   // 부모의 children 에서 빠짐. 자식 링크도 끊지만 자식들은 아래에서 함께 지워짐
	m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(),
		[&doomed](const std::unique_ptr<GameObject>& o) { return std::find(doomed.begin(), doomed.end(), o.get()) != doomed.end(); }), m_objects.end());
	if (doomed.size() > 1) Log::Info("오브젝트 삭제: 자식 %zu 개 포함", doomed.size() - 1);
}

size_t Scene::IndexOf(const GameObject* object) const
{
	for (size_t i = 0; i < m_objects.size(); ++i) if (m_objects[i].get() == object) return i;
	return SIZE_MAX;
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
		XMStoreFloat4(&m_blendFrom.rotation, XMQuaternionRotationMatrix(camera->GetRotationMatrix()));   // 11-F단계: 행렬 → 쿼터니언 (보간용)
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
		// 11-F단계: 회전은 쿼터니언 slerp. 이전의 yaw/pitch 별 lerp 는 roll 을 표현할 수 없고, 두 각을 따로 보간하면 수직 근처에서 경로가 휘었음.
		// slerp 는 언제나 두 자세 사이의 최단 호를 따르므로 "yaw 를 짧은 쪽으로 감기" 처리도 필요 없음.
		const XMVECTOR from = XMLoadFloat4(&m_blendFrom.rotation);
		const XMVECTOR to = XMLoadFloat4(&pose.rotation);
		XMStoreFloat4(&pose.rotation, XMQuaternionNormalize(XMQuaternionSlerp(from, to, t)));
		pose.fovY = m_blendFrom.fovY + (pose.fovY - m_blendFrom.fovY) * t;
	}
	camera->SetPosition(pose.position);
	camera->SetRotation(pose.GetRotationMatrix());
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

	// 11-F단계: 노드가 여럿이면 빈 루트 오브젝트 하나를 만들어 그 아래에 인스턴스를 자식으로 둠 (유니티가 모델 프리팹을 놓을 때와 같음).
	// 루트가 placement(transform)를 갖고 자식은 로컬 = pre(노드 월드 행렬) 이므로 결과 월드 행렬은 이전(pre × placement)과 정확히 같다.
	// 이제 루트 하나를 옮기면 모델 전체가 따라오고, Hierarchy 에서 접힌다. 노드가 하나면 루트 없이 그 오브젝트가 placement 를 가짐 (이전과 동일).
	size_t validInstances = 0;
	for (const ModelInstance& instance : model.instances) if (instance.meshIndex < meshes.size()) ++validInstances;
	GameObject* root = nullptr;
	if (validInstances > 1)
	{
		std::string rootName = Log::ToUtf8(model.sourcePath.c_str());
		rootName = rootName.substr(rootName.find_last_of("\\/") + 1);
		root = &AddObject(nullptr, nullptr, rootName.c_str());
		root->GetTransform() = transform;
	}

	size_t created = root != nullptr ? 1 : 0;
	for (const ModelInstance& instance : model.instances)
	{
		if (instance.meshIndex >= meshes.size()) continue;
		GameObject& object = AddObject(meshes[instance.meshIndex], materials.empty() ? nullptr : materials.back(), instance.name.c_str());
		std::vector<const Material*> slots = materials;   // 슬롯 번호 = 모델 재질 인덱스
		object.SetSlotMaterials(std::move(slots));
		if (root != nullptr) object.SetParent(root, false);   // 로컬 값은 pre 만. keepWorld=false: 아래에서 pre 를 직접 넣음
		else object.GetTransform() = transform;
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
	environment = SceneEnvironment{};   // 2026-10-08: 씬 빌더·씬 파일이 다시 정함. 정하지 않으면 스카이박스·IBL 없음
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

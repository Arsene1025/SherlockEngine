#include "pch.h"
#include "Scene/SceneSerializer.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Graphics/Mesh.h"
#include "Graphics/Model.h"
#include "Core/AssetManager.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <unordered_map>

using json = nlohmann::json;
using namespace DirectX;   // 이 파일 안에서만

namespace
{
	constexpr int kVersion = 5;   // 5: 2026-10-15 오브젝트 "id"(안정 ID)와 "prefab"(프리팹 인스턴스 루트). 2: 11-C단계 컴포넌트 추가. 3: 11-F단계 "parent"(부모 오브젝트 인덱스). 4: 2026-10-08 "environment"(스카이박스·IBL·노출)와 PBR 재질. 이전 버전 파일도 그대로 읽힘 (필드 없음 = 루트, 환경 없음)

	json ToJson(const XMFLOAT3& v) { return json::array({ v.x, v.y, v.z }); }
	json ToJson(const XMFLOAT4& v) { return json::array({ v.x, v.y, v.z, v.w }); }
	json ToJson(const XMFLOAT2& v) { return json::array({ v.x, v.y }); }
	json ToJson(const XMFLOAT4X4& m)
	{
		json a = json::array();
		for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) a.push_back(m.m[r][c]);
		return a;
	}

	XMFLOAT3 ToFloat3(const json& j, const XMFLOAT3& fallback)
	{
		if (!j.is_array() || j.size() < 3) return fallback;
		return XMFLOAT3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
	}
	XMFLOAT4 ToFloat4(const json& j, const XMFLOAT4& fallback)
	{
		if (!j.is_array() || j.size() < 4) return fallback;
		return XMFLOAT4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
	}
	XMFLOAT2 ToFloat2(const json& j, const XMFLOAT2& fallback)
	{
		if (!j.is_array() || j.size() < 2) return fallback;
		return XMFLOAT2(j[0].get<float>(), j[1].get<float>());
	}

	std::string ToUtf8(const std::wstring& wide) { return Log::ToUtf8(wide.c_str()); }
	std::wstring ToWide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
		return wide;
	}

	const char* SourceTypeName(MeshSource::Type type)
	{
		switch (type)
		{
		case MeshSource::Type::Sphere: return "sphere";
		case MeshSource::Type::Cube: return "cube";
		case MeshSource::Type::Plane: return "plane";
		case MeshSource::Type::Cylinder: return "cylinder";
		case MeshSource::Type::Model: return "model";
		default: return "custom";
		}
	}

	MeshSource::Type ParseSourceType(const std::string& name)
	{
		if (name == "sphere") return MeshSource::Type::Sphere;
		if (name == "cube") return MeshSource::Type::Cube;
		if (name == "plane") return MeshSource::Type::Plane;
		if (name == "cylinder") return MeshSource::Type::Cylinder;
		if (name == "model") return MeshSource::Type::Model;
		return MeshSource::Type::Custom;
	}

	json MaterialToJson(const Material& m)
	{
		json j;
		j["name"] = m.name;
		j["baseColor"] = ToJson(m.baseColor);
		j["metallic"] = m.metallic;     // 2026-10-08 PBR
		j["roughness"] = m.roughness;
		j["emissive"] = ToJson(m.emissive);
		j["metallicRoughnessTexture"] = m.metallicRoughnessTexture;
		j["occlusionTexture"] = m.occlusionTexture;
		j["occlusionStrength"] = m.occlusionStrength;
		j["emissiveTexture"] = m.emissiveTexture;
		j["albedoTexture"] = m.albedoTexture;
		j["albedoSrgb"] = m.albedoSrgb;
		j["sampler"] = static_cast<int>(m.sampler);
		j["uvScale"] = ToJson(m.uvScale);
		j["unlit"] = m.unlit;
		j["normalTexture"] = m.normalTexture;
		j["normalStrength"] = m.normalStrength;
		j["alphaCutoff"] = m.alphaCutoff;
		j["wireframe"] = m.wireframe;
		j["doubleSided"] = m.doubleSided;
		return j;
	}

	Material MaterialFromJson(const json& j)
	{
		Material m;
		m.name = j.value("name", "");
		m.baseColor = ToFloat4(j.value("baseColor", json()), m.baseColor);
		m.metallic = j.value("metallic", m.metallic);
		if (j.contains("roughness")) m.roughness = j.value("roughness", m.roughness);
		else if (j.contains("shininess"))
		{
			// 2026-10-08 이전(Blinn-Phong) 파일: 광택 지수 → 거칠기. Blinn-Phong 지수 n 과 Beckmann 거칠기 m 의 관계 n = 2/m² − 2 를
			// 거꾸로 풀고(m = sqrt(2/(n+2))), GGX 의 α ≈ m 이므로 지각적 거칠기 = sqrt(m). specularColor 는 버림 (유전체 F0 = 0.04).
			const float shininess = (std::max)(j.value("shininess", 32.0f), 1.0f);
			m.roughness = std::sqrt(std::sqrt(2.0f / (shininess + 2.0f)));
		}
		m.emissive = ToFloat3(j.value("emissive", json()), m.emissive);
		m.metallicRoughnessTexture = j.value("metallicRoughnessTexture", m.metallicRoughnessTexture);
		m.occlusionTexture = j.value("occlusionTexture", m.occlusionTexture);
		m.occlusionStrength = j.value("occlusionStrength", m.occlusionStrength);
		m.emissiveTexture = j.value("emissiveTexture", m.emissiveTexture);
		m.albedoTexture = j.value("albedoTexture", "");
		m.albedoSrgb = j.value("albedoSrgb", true);
		m.sampler = static_cast<SamplerPreset>(j.value("sampler", 0));
		m.uvScale = ToFloat2(j.value("uvScale", json()), m.uvScale);
		m.unlit = j.value("unlit", false);
		m.normalTexture = j.value("normalTexture", "");
		m.normalStrength = j.value("normalStrength", 1.0f);
		m.alphaCutoff = j.value("alphaCutoff", 0.0f);
		m.wireframe = j.value("wireframe", false);
		m.doubleSided = j.value("doubleSided", false);
		return m;
	}

	json LightToJson(const LightData& l)
	{
		json j;
		j["type"] = l.type;
		j["position"] = ToJson(l.position);
		j["direction"] = ToJson(l.direction);
		j["color"] = ToJson(l.color);
		j["intensity"] = l.intensity;
		j["range"] = l.range;
		j["attenuation"] = ToJson(l.attenuation);
		j["innerConeCos"] = l.innerConeCos;
		j["outerConeCos"] = l.outerConeCos;
		return j;
	}

	LightData LightFromJson(const json& j)
	{
		LightData l;
		l.type = j.value("type", 0u);
		l.position = ToFloat3(j.value("position", json()), l.position);
		l.direction = ToFloat3(j.value("direction", json()), l.direction);
		l.color = ToFloat3(j.value("color", json()), l.color);
		l.intensity = j.value("intensity", l.intensity);
		l.range = j.value("range", l.range);
		l.attenuation = ToFloat3(j.value("attenuation", json()), l.attenuation);
		l.innerConeCos = j.value("innerConeCos", l.innerConeCos);
		l.outerConeCos = j.value("outerConeCos", l.outerConeCos);
		return l;
	}

	// 11-C단계: 컴포넌트 필드 ↔ JSON 변환. Reflect 가 열거하는 필드 이름을 그대로 JSON 키로 씀.
	class JsonWriteVisitor : public PropertyVisitor
	{
	public:
		explicit JsonWriteVisitor(json& j) : m_j(j) {}
		void Float(const char* name, float& value, float) override { m_j[name] = value; }
		void Int(const char* name, int& value) override { m_j[name] = value; }
		void Bool(const char* name, bool& value) override { m_j[name] = value; }
		void Float3(const char* name, XMFLOAT3& value, float) override { m_j[name] = ToJson(value); }
		void String(const char* name, std::string& value) override { m_j[name] = value; }
	private:
		json& m_j;
	};
	class JsonReadVisitor : public PropertyVisitor
	{
	public:
		explicit JsonReadVisitor(const json& j) : m_j(j) {}
		void Float(const char* name, float& value, float) override { if (m_j.contains(name) && m_j[name].is_number()) value = m_j[name].get<float>(); }
		void Int(const char* name, int& value) override { if (m_j.contains(name) && m_j[name].is_number()) value = m_j[name].get<int>(); }
		void Bool(const char* name, bool& value) override { if (m_j.contains(name) && m_j[name].is_boolean()) value = m_j[name].get<bool>(); }
		void Float3(const char* name, XMFLOAT3& value, float) override { if (m_j.contains(name)) value = ToFloat3(m_j[name], value); }
		void String(const char* name, std::string& value) override { if (m_j.contains(name) && m_j[name].is_string()) value = m_j[name].get<std::string>(); }
	private:
		const json& m_j;
	};

	json ComponentsToJson(const GameObject& object)
	{
		json components = json::array();
		for (const auto& behaviour : object.GetBehaviours())
		{
			json c;
			c["type"] = behaviour->GetTypeName();
			c["enabled"] = behaviour->enabled;
			JsonWriteVisitor writer(c);
			const_cast<Behaviour&>(*behaviour).Reflect(writer);   // Reflect 는 편집용이라 non-const 임. 쓰기 방문자는 값을 바꾸지 않음
			components.push_back(c);
		}
		// 2026-10-08 (B안): 만들지 못한 컴포넌트(스크립트 DLL 이 없음)는 읽은 그대로 다시 씀
		for (const std::string& missing : object.GetMissingComponents())
		{
			json c = json::parse(missing, nullptr, false);
			if (!c.is_discarded()) components.push_back(std::move(c));
		}
		return components;
	}

	void ComponentsFromJson(GameObject& object, const json& components)
	{
		if (!components.is_array()) return;
		for (const json& c : components)
		{
			Behaviour* behaviour = object.AddBehaviour(c.value("type", ""));
			if (behaviour == nullptr)
			{
				// 모르는 타입(레지스트리가 경고를 남김). 2026-10-08 (B안): 버리지 않고 원본을 보관해 저장·핫리로드 때 되살림
				object.GetMissingComponents().push_back(c.dump());
				continue;
			}
			behaviour->enabled = c.value("enabled", true);
			JsonReadVisitor reader(c);
			behaviour->Reflect(reader);
		}
	}
}

// ------------------------------------------------------------------ 2026-10-15: 오브젝트·서브트리 단위 직렬화
// 씬 저장, 되돌리기 스냅샷, 프리팹 저장·생성, 복제가 아래 도구를 함께 씀. 모두 "오브젝트 → JSON" 과 "JSON → 오브젝트" 두 방향이고,
// 메시·재질·부모는 호출자가 정한 인덱스(씬 전체 기준이거나 서브트리 안의 지역 기준)로 참조함.
namespace
{
	json MeshSourceToJson(const Mesh& mesh, const MeshSource* source)
	{
		json j;
		if (source == nullptr || source->type == MeshSource::Type::Custom)
		{
			j["type"] = "custom";
			Log::Warn("씬 저장: 출처를 모르는 메시(정점 %u)는 로드 시 비어 있게 된다.", mesh.GetVertexCount());
			return j;
		}
		j["type"] = SourceTypeName(source->type);
		if (source->type == MeshSource::Type::Model)
		{
			j["path"] = ToUtf8(source->modelPath);
			j["meshIndex"] = source->meshIndex;
		}
		else
		{
			j["params"] = json::array({ source->params[0], source->params[1], source->params[2], source->params[3] });
			j["a"] = source->a;
			j["b"] = source->b;
			j["color"] = ToJson(source->color);
		}
		return j;
	}

	// 출처 JSON 으로 메시를 씬에 만듦. 모델 메시는 AssetManager 에서 복사하고, 모델의 이미지는 모델마다 한 번만 씬에 넣음(loadedModels 로 추적).
	// 실패하면 nullptr (호출자가 인덱스 자리를 nullptr 로 유지함).
	const Mesh* MeshFromJson(Scene& scene, AssetManager& assets, const json& j, std::vector<std::wstring>& loadedModels)
	{
		MeshSource source;
		source.type = ParseSourceType(j.value("type", "custom"));
		if (source.type == MeshSource::Type::Model)
		{
			source.modelPath = ToWide(j.value("path", ""));
			source.meshIndex = j.value("meshIndex", 0u);
			const Model* model = assets.GetModel(source.modelPath);
			if (model == nullptr || source.meshIndex >= model->meshes.size())
			{
				Log::Warn("씬 로드: 모델 메시를 찾지 못함 (%s #%u)", j.value("path", "").c_str(), source.meshIndex);
				return nullptr;
			}
			Mesh copy = model->meshes[source.meshIndex];
			const Mesh* created = scene.AddMesh(std::move(copy), source);
			if (std::find(loadedModels.begin(), loadedModels.end(), source.modelPath) == loadedModels.end())
			{
				scene.AddModelImages(*model);
				loadedModels.push_back(source.modelPath);
			}
			return created;
		}
		if (source.type == MeshSource::Type::Custom)
		{
			Log::Warn("씬 로드: 출처 없는(custom) 메시는 비어 있다.");
			return nullptr;
		}
		const json params = j.value("params", json::array());
		for (size_t i = 0; i < 4 && i < params.size(); ++i) source.params[i] = params[i].get<float>();
		source.a = j.value("a", 0u);
		source.b = j.value("b", 0u);
		source.color = ToFloat4(j.value("color", json()), source.color);
		Mesh mesh;
		return source.CreatePrimitive(mesh) ? scene.AddMesh(std::move(mesh), source) : nullptr;
	}

	// 포인터 → 인덱스 (없으면 −1). 씬 전체 기준 맵이거나 서브트리의 지역 목록임.
	template <typename T>
	using IndexMap = std::unordered_map<const T*, int>;
	template <typename T>
	int IndexIn(const IndexMap<T>& map, const T* pointer)
	{
		const auto found = map.find(pointer);
		return found == map.end() ? -1 : found->second;
	}

	// 오브젝트 하나 → JSON. parentIndex: 같은 배열 안의 부모 위치 (−1 = 루트라 키를 쓰지 않음). withId: 안정 ID 를 저장할지
	// (씬·되돌리기는 저장하고, 프리팹은 인스턴스마다 새 ID 를 받으므로 저장하지 않음).
	json ObjectToJson(const GameObject& object, int parentIndex, bool withId, const IndexMap<Mesh>& meshes, const IndexMap<Material>& materials)
	{
		json j;
		j["name"] = object.GetName();
		if (withId) j["id"] = object.GetId();
		if (parentIndex >= 0) j["parent"] = parentIndex;
		j["mesh"] = IndexIn(meshes, object.GetMesh());
		j["material"] = IndexIn(materials, object.GetMaterial());
		if (!object.GetSlotMaterials().empty())
		{
			json slots = json::array();
			for (const Material* slot : object.GetSlotMaterials()) slots.push_back(IndexIn(materials, slot));
			j["slotMaterials"] = slots;
		}
		const Transform& t = object.GetTransform();
		j["position"] = ToJson(t.GetPosition());
		j["rotation"] = ToJson(t.GetRotation());
		j["scale"] = ToJson(t.GetScale());
		if (t.HasPreTransform())
		{
			XMFLOAT4X4 pre;
			XMStoreFloat4x4(&pre, t.GetPreTransform());
			j["preTransform"] = ToJson(pre);
		}
		if (!object.GetBehaviours().empty() || !object.GetMissingComponents().empty()) j["components"] = ComponentsToJson(object);   // 11-C단계
		if (!object.GetPrefabSource().empty()) j["prefab"] = object.GetPrefabSource();
		return j;
	}

	// JSON → 오브젝트 하나. 부모 연결은 호출자가 모든 오브젝트를 만든 뒤 함 (부모가 배열에서 뒤에 있을 수 있음).
	// useId: 파일의 ID 를 그대로 쓰되, 0 이거나 씬에 이미 있으면 새 ID 를 유지함.
	GameObject& ObjectFromJson(Scene& scene, const json& j, bool useId, const std::vector<const Mesh*>& meshes, const std::vector<const Material*>& materials)
	{
		const auto materialAt = [&materials](int index) -> const Material* { return (index >= 0 && index < static_cast<int>(materials.size())) ? materials[index] : nullptr; };
		const int mi = j.value("mesh", -1);
		const Mesh* mesh = (mi >= 0 && mi < static_cast<int>(meshes.size())) ? meshes[mi] : nullptr;
		const std::string name = j.value("name", "");
		GameObject& object = scene.AddObject(mesh, materialAt(j.value("material", -1)), name.c_str());
		if (useId)
		{
			const uint64_t id = j.value("id", static_cast<uint64_t>(0));
			if (id != 0 && scene.FindById(id) == nullptr) object.SetId(id);
		}
		if (j.contains("slotMaterials"))
		{
			std::vector<const Material*> slots;
			for (const json& s : j["slotMaterials"]) slots.push_back(materialAt(s.get<int>()));
			object.SetSlotMaterials(std::move(slots));
		}
		Transform& t = object.GetTransform();
		t.SetPosition(ToFloat3(j.value("position", json()), XMFLOAT3(0.0f, 0.0f, 0.0f)));
		t.SetRotation(ToFloat3(j.value("rotation", json()), XMFLOAT3(0.0f, 0.0f, 0.0f)));
		t.SetScale(ToFloat3(j.value("scale", json()), XMFLOAT3(1.0f, 1.0f, 1.0f)));
		if (j.contains("preTransform") && j["preTransform"].is_array() && j["preTransform"].size() == 16)
		{
			XMFLOAT4X4 pre;
			for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) pre.m[r][c] = j["preTransform"][r * 4 + c].get<float>();
			t.SetPreTransform(XMLoadFloat4x4(&pre));
		}
		if (j.contains("components")) ComponentsFromJson(object, j["components"]);   // 11-C단계
		object.SetPrefabSource(j.value("prefab", std::string()));
		return object;
	}

	// 오브젝트 배열의 가장 큰 ID. 새로 부여할 ID 가 파일의 ID 와 겹치지 않도록 오브젝트를 만들기 전에 Scene::ReserveIds 에 넘김.
	uint64_t MaxIdIn(const json& objects)
	{
		uint64_t maxId = 0;
		if (objects.is_array()) for (const json& j : objects) maxId = (std::max)(maxId, j.value("id", static_cast<uint64_t>(0)));
		return maxId;
	}

	// created[i] 의 부모를 parents[i] (created 안의 인덱스, −1 = 루트)로 연결함. 저장된 값이 이미 로컬이므로 keepWorld = false.
	void LinkParents(const std::vector<GameObject*>& created, const std::vector<int>& parents)
	{
		for (size_t i = 0; i < parents.size(); ++i)
		{
			const int parent = parents[i];
			if (parent < 0 || parent >= static_cast<int>(created.size()) || parent == static_cast<int>(i)) continue;
			if (!created[i]->SetParent(created[parent], false))   // 순환이면 SetParent 가 거부함 (손으로 고친 파일)
				Log::Warn("씬 로드: 오브젝트 %zu 의 부모 %d 가 순환을 만들어 무시함", i, parent);
		}
	}

	// 오브젝트 배열을 만들어 씬에 넣고 생성한 포인터를 돌려줌 (부모 연결 포함).
	std::vector<GameObject*> CreateObjects(Scene& scene, const json& objects, bool useId, const std::vector<const Mesh*>& meshes, const std::vector<const Material*>& materials)
	{
		if (useId) scene.ReserveIds(MaxIdIn(objects));
		std::vector<GameObject*> created;
		std::vector<int> parents;
		if (objects.is_array())
		{
			for (const json& j : objects)
			{
				parents.push_back(j.value("parent", -1));
				created.push_back(&ObjectFromJson(scene, j, useId, meshes, materials));
			}
		}
		LinkParents(created, parents);
		return created;
	}

	json EnvironmentToJson(const SceneEnvironment& env)
	{
		return {
			{ "skybox", env.skybox }, { "irradiance", env.irradiance }, { "specular", env.specular }, { "brdfLut", env.brdfLut },
			{ "iblIntensity", env.iblIntensity }, { "exposure", env.exposure }, { "toneMapping", env.toneMapping },
			{ "postProcess", {
				{ "blurEnabled", env.post.blurEnabled }, { "blurRadius", env.post.blurRadius },
				{ "bloomEnabled", env.post.bloomEnabled }, { "bloomThreshold", env.post.bloomThreshold },
				{ "bloomKnee", env.post.bloomKnee }, { "bloomIntensity", env.post.bloomIntensity },
				{ "greyscale", env.post.greyscale },
			} },
		};
	}

	void ApplyEnvironment(Scene& scene, const json& env)
	{
		SceneEnvironment& e = scene.environment;
		e.skybox = env.value("skybox", std::string());
		e.irradiance = env.value("irradiance", std::string());
		e.specular = env.value("specular", std::string());
		e.brdfLut = env.value("brdfLut", std::string());
		e.iblIntensity = env.value("iblIntensity", 1.0f);
		e.exposure = env.value("exposure", 1.0f);
		e.toneMapping = env.value("toneMapping", false);
		// 2026/10/23: 후처리. 키가 없으면(이전 씬 파일) 전부 꺼진 기본값
		const PostProcessSettings defaults;
		const json post = env.value("postProcess", json::object());
		e.post.blurEnabled = post.value("blurEnabled", defaults.blurEnabled);
		e.post.blurRadius = post.value("blurRadius", defaults.blurRadius);
		e.post.bloomEnabled = post.value("bloomEnabled", defaults.bloomEnabled);
		e.post.bloomThreshold = post.value("bloomThreshold", defaults.bloomThreshold);
		e.post.bloomKnee = post.value("bloomKnee", defaults.bloomKnee);
		e.post.bloomIntensity = post.value("bloomIntensity", defaults.bloomIntensity);
		e.post.greyscale = post.value("greyscale", defaults.greyscale);
	}

	void ApplyAmbientAndClear(Scene& scene, const json& root)
	{
		scene.ambientColor = ToFloat3(root.value("ambientColor", json()), scene.ambientColor);
		const json clear = root.value("clearColor", json());
		if (clear.is_array() && clear.size() >= 4) for (int i = 0; i < 4; ++i) scene.clearColor[i] = clear[i].get<float>();
	}

	// 서브트리(루트와 모든 자손)를 부모 먼저(깊이 우선) 순서로 모음. 프리팹 저장과 복제가 씀.
	struct Subtree
	{
		std::vector<const GameObject*> objects;      // objects[0] = 루트
		std::vector<const Mesh*> meshes;             // 서브트리가 쓰는 메시 (첫 사용 순서)
		std::vector<const Material*> materials;      // 서브트리가 쓰는 재질 (기본 재질과 슬롯 재질)
		json objectsJson = json::array();            // 오브젝트 JSON. 메시·재질·부모 인덱스는 위 목록 기준
	};

	Subtree CollectSubtree(const GameObject& root, bool withId)
	{
		Subtree tree;
		const auto walk = [&tree](const GameObject& object, auto& self) -> void
		{
			tree.objects.push_back(&object);
			for (const GameObject* child : object.GetChildren()) self(*child, self);
		};
		walk(root, walk);

		IndexMap<GameObject> objectIndex;
		IndexMap<Mesh> meshIndex;
		IndexMap<Material> materialIndex;
		const auto addMesh = [&](const Mesh* mesh)
		{
			if (mesh != nullptr && !meshIndex.count(mesh)) { meshIndex[mesh] = static_cast<int>(tree.meshes.size()); tree.meshes.push_back(mesh); }
		};
		const auto addMaterial = [&](const Material* material)
		{
			if (material != nullptr && !materialIndex.count(material)) { materialIndex[material] = static_cast<int>(tree.materials.size()); tree.materials.push_back(material); }
		};
		for (size_t i = 0; i < tree.objects.size(); ++i)
		{
			const GameObject& object = *tree.objects[i];
			objectIndex[&object] = static_cast<int>(i);
			addMesh(object.GetMesh());
			addMaterial(object.GetMaterial());
			for (const Material* slot : object.GetSlotMaterials()) addMaterial(slot);
		}
		for (size_t i = 0; i < tree.objects.size(); ++i)
		{
			const GameObject& object = *tree.objects[i];
			const int parent = i == 0 ? -1 : IndexIn(objectIndex, object.GetParent());
			tree.objectsJson.push_back(ObjectToJson(object, parent, withId, meshIndex, materialIndex));
		}
		return tree;
	}

	// ---- 프리팹 문서 캐시: 파일을 한 번만 읽고 파싱함. 수정 시각이 바뀌면 다시 읽음 (에디터가 같은 프리팹을 다시 저장한 경우) ----
	struct PrefabDocument
	{
		uint64_t writeTime = 0;
		std::shared_ptr<const json> document;
	};

	uint64_t FileWriteTime(const std::wstring& path)
	{
		WIN32_FILE_ATTRIBUTE_DATA data{};
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
		return (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
	}

	std::shared_ptr<const json> LoadPrefabDocument(const std::wstring& fullPath, uint64_t& outWriteTime)
	{
		static std::unordered_map<std::wstring, PrefabDocument> cache;
		outWriteTime = FileWriteTime(fullPath);
		if (outWriteTime == 0)
		{
			Log::Error("프리팹을 찾지 못함: %s", ToUtf8(fullPath).c_str());
			return nullptr;
		}
		PrefabDocument& entry = cache[fullPath];
		if (entry.document != nullptr && entry.writeTime == outWriteTime) return entry.document;
		std::ifstream file(fullPath, std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		json root = json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object() || root.value("type", "") != "prefab")
		{
			Log::Error("프리팹 형식 오류: %s", ToUtf8(fullPath).c_str());
			cache.erase(fullPath);
			return nullptr;
		}
		entry.writeTime = outWriteTime;
		entry.document = std::make_shared<const json>(std::move(root));
		return entry.document;
	}
}

std::string SceneSerializer::SaveToString(const Scene& scene, const Camera& camera)
{
	json root;
	root["version"] = kVersion;
	root["camera"] = { { "position", ToJson(camera.GetPosition()) }, { "yaw", camera.GetYaw() }, { "pitch", camera.GetPitch() } };
	root["ambientColor"] = ToJson(scene.ambientColor);
	root["clearColor"] = json::array({ scene.clearColor[0], scene.clearColor[1], scene.clearColor[2], scene.clearColor[3] });
	root["environment"] = EnvironmentToJson(scene.environment);   // 2026-10-08. 키가 없으면 환경 없음 (이전 씬 파일과 호환)

	// 메시 출처. 인덱스 = 씬의 메시 순서.
	IndexMap<Mesh> meshIndex;
	json meshes = json::array();
	for (const auto& mesh : scene.GetMeshes())
	{
		meshIndex[mesh.get()] = static_cast<int>(meshes.size());
		meshes.push_back(MeshSourceToJson(*mesh, scene.GetMeshSource(mesh.get())));
	}
	root["meshes"] = meshes;

	IndexMap<Material> materialIndex;
	json materials = json::array();
	for (const auto& material : scene.GetMaterials())
	{
		materialIndex[material.get()] = static_cast<int>(materials.size());
		materials.push_back(MaterialToJson(*material));
	}
	root["materials"] = materials;

	json lights = json::array();
	for (const LightData& light : scene.GetLights()) lights.push_back(LightToJson(light));
	root["lights"] = lights;

	// 11-F단계: 부모는 objects 배열의 인덱스로 (자식이 부모보다 앞에 올 수 있으므로 로드는 두 단계). position/rotation/scale 은 부모 기준 로컬 값.
	IndexMap<GameObject> objectIndex;
	for (size_t i = 0; i < scene.GetObjects().size(); ++i) objectIndex[scene.GetObjects()[i].get()] = static_cast<int>(i);
	json objects = json::array();
	for (const auto& object : scene.GetObjects())
		objects.push_back(ObjectToJson(*object, object->GetParent() != nullptr ? IndexIn(objectIndex, object->GetParent()) : -1, true, meshIndex, materialIndex));
	root["objects"] = objects;
	return root.dump(2);
}

bool SceneSerializer::Save(const Scene& scene, const Camera& camera, const std::wstring& path)
{
	const std::string text = SaveToString(scene, camera);
	const size_t slash = path.find_last_of(L"\\/");
	if (slash != std::wstring::npos) CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file)
	{
		Log::Error("씬 저장 실패: 파일을 열 수 없음 (%s)", ToUtf8(path).c_str());
		return false;
	}
	file << text;
	Log::Info("씬 저장: %s (오브젝트 %zu, 메시 %zu, 재질 %zu, 조명 %zu)", ToUtf8(path).c_str(),
		scene.GetObjects().size(), scene.GetMeshes().size(), scene.GetMaterials().size(), scene.GetLights().size());
	return true;
}

bool SceneSerializer::Load(Scene& scene, Camera& camera, AssetManager& assets, const std::wstring& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
	{
		Log::Error("씬 로드 실패: 파일을 열 수 없음 (%s)", ToUtf8(path).c_str());
		return false;
	}
	const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	return LoadFromString(scene, camera, assets, text, ToUtf8(path).c_str());
}

bool SceneSerializer::LoadFromString(Scene& scene, Camera& camera, AssetManager& assets, const std::string& text, const char* label)
{
	json root;
	try
	{
		root = json::parse(text);
	}
	catch (const std::exception& e)
	{
		Log::Error("씬 로드 실패: JSON 파싱 오류 (%s): %s", label, e.what());
		return false;
	}
	if (root.value("version", 0) > kVersion)
	{
		Log::Warn("씬 파일 버전 %d (지원 %d). 계속 시도한다.", root.value("version", 0), kVersion);
	}

	scene.Clear();

	// ---- 메시. 모델 메시는 AssetManager 에서 가져옴 (이미지도 함께). nullptr 자리도 유지 (인덱스 보존) ----
	std::vector<const Mesh*> meshes;
	std::vector<std::wstring> loadedModels;
	for (const json& j : root.value("meshes", json::array())) meshes.push_back(MeshFromJson(scene, assets, j, loadedModels));

	std::vector<const Material*> materials;
	for (const json& j : root.value("materials", json::array())) materials.push_back(scene.AddMaterial(MaterialFromJson(j)));

	for (const json& j : root.value("lights", json::array()))
	{
		if (scene.GetLights().size() < MAX_LIGHTS) scene.GetLights().push_back(LightFromJson(j));
	}

	CreateObjects(scene, root.value("objects", json::array()), true, meshes, materials);

	ApplyAmbientAndClear(scene, root);
	const json env = root.value("environment", json());
	if (env.is_object()) ApplyEnvironment(scene, env);
	else scene.environment.skybox = root.value("skybox", std::string());   // 같은 날 잠시 쓴 형식 (루트의 "skybox" 하나)

	const json cam = root.value("camera", json());
	if (cam.is_object())
	{
		camera.SetPosition(ToFloat3(cam.value("position", json()), camera.GetPosition()));
		camera.SetYawPitch(cam.value("yaw", camera.GetYaw()), cam.value("pitch", camera.GetPitch()));
	}

	Log::Info("씬 로드: %s (오브젝트 %zu, 메시 %zu, 재질 %zu, 조명 %zu, 모델 %zu)", label,
		scene.GetObjects().size(), scene.GetMeshes().size(), scene.GetMaterials().size(), scene.GetLights().size(), loadedModels.size());
	return true;
}

// ------------------------------------------------------------------ 2026-10-15: 편집 상태 (되돌리기)

std::string SceneSerializer::SaveEditState(const Scene& scene)
{
	json root;
	root["ambientColor"] = ToJson(scene.ambientColor);
	root["clearColor"] = json::array({ scene.clearColor[0], scene.clearColor[1], scene.clearColor[2], scene.clearColor[3] });
	root["environment"] = EnvironmentToJson(scene.environment);

	IndexMap<Mesh> meshIndex;
	for (size_t i = 0; i < scene.GetMeshes().size(); ++i) meshIndex[scene.GetMeshes()[i].get()] = static_cast<int>(i);
	IndexMap<Material> materialIndex;
	json materials = json::array();
	for (size_t i = 0; i < scene.GetMaterials().size(); ++i)
	{
		materialIndex[scene.GetMaterials()[i].get()] = static_cast<int>(i);
		materials.push_back(MaterialToJson(*scene.GetMaterials()[i]));
	}
	root["materials"] = materials;

	json lights = json::array();
	for (const LightData& light : scene.GetLights()) lights.push_back(LightToJson(light));
	root["lights"] = lights;

	IndexMap<GameObject> objectIndex;
	for (size_t i = 0; i < scene.GetObjects().size(); ++i) objectIndex[scene.GetObjects()[i].get()] = static_cast<int>(i);
	json objects = json::array();
	for (const auto& object : scene.GetObjects())
		objects.push_back(ObjectToJson(*object, object->GetParent() != nullptr ? IndexIn(objectIndex, object->GetParent()) : -1, true, meshIndex, materialIndex));
	root["objects"] = objects;
	return root.dump();   // 이력에 많이 쌓이므로 들여쓰기 없이
}

bool SceneSerializer::RestoreEditState(Scene& scene, const std::string& text)
{
	const json root = json::parse(text, nullptr, false);
	if (root.is_discarded() || !root.is_object()) return false;

	// 메시 목록은 그대로 둠 (되돌리기 이력은 메시·재질이 늘기만 하는 동안만 유효 — 씬을 비우는 경로는 이력도 비움).
	std::vector<const Mesh*> meshes;
	for (const auto& mesh : scene.GetMeshes()) meshes.push_back(mesh.get());

	// 재질은 같은 인덱스의 것을 제자리에서 덮어씀: 주소가 그대로여야 Renderer 캐시와 다른 오브젝트의 참조가 유지됨.
	size_t index = 0;
	for (const json& j : root.value("materials", json::array()))
	{
		if (index >= scene.GetMaterials().size()) break;
		*scene.GetMaterials()[index] = MaterialFromJson(j);
		++index;
	}
	std::vector<const Material*> materials;
	for (const auto& material : scene.GetMaterials()) materials.push_back(material.get());

	scene.GetLights().clear();
	for (const json& j : root.value("lights", json::array()))
	{
		if (scene.GetLights().size() < MAX_LIGHTS) scene.GetLights().push_back(LightFromJson(j));
	}
	ApplyAmbientAndClear(scene, root);
	const json env = root.value("environment", json());
	if (env.is_object()) ApplyEnvironment(scene, env);

	scene.ClearObjects();
	CreateObjects(scene, root.value("objects", json::array()), true, meshes, materials);
	return true;
}

// ------------------------------------------------------------------ 2026-10-15: 서브트리 · 프리팹

GameObject* SceneSerializer::CloneSubtree(Scene& scene, const GameObject& source)
{
	// 같은 씬 안의 복제: 메시·재질은 새로 만들지 않고 같은 포인터를 공유함 (유니티의 sharedMaterial). ID 는 새로 받음.
	// 서브트리 JSON 에는 루트의 parent 키가 없으므로 복제본의 루트는 언제나 최상위 오브젝트임.
	const Subtree tree = CollectSubtree(source, false);
	std::vector<GameObject*> created = CreateObjects(scene, tree.objectsJson, false, tree.meshes, tree.materials);
	if (created.empty()) return nullptr;
	created[0]->SetName(source.GetName() + " (Clone)");
	return created[0];
}

bool SceneSerializer::SavePrefab(const Scene& scene, const GameObject& root, const std::wstring& path)
{
	const Subtree tree = CollectSubtree(root, false);
	json doc;
	doc["version"] = 1;
	doc["type"] = "prefab";
	doc["name"] = root.GetName();
	json meshes = json::array();
	for (const Mesh* mesh : tree.meshes) meshes.push_back(MeshSourceToJson(*mesh, scene.GetMeshSource(mesh)));
	doc["meshes"] = meshes;
	json materials = json::array();
	for (const Material* material : tree.materials) materials.push_back(MaterialToJson(*material));
	doc["materials"] = materials;
	doc["objects"] = tree.objectsJson;

	const size_t slash = path.find_last_of(L"\\/");
	if (slash != std::wstring::npos) CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file)
	{
		Log::Error("프리팹 저장 실패: 파일을 열 수 없음 (%s)", ToUtf8(path).c_str());
		return false;
	}
	file << doc.dump(2);
	Log::Info("프리팹 저장: %s (오브젝트 %zu, 메시 %zu, 재질 %zu)", ToUtf8(path).c_str(), tree.objects.size(), tree.meshes.size(), tree.materials.size());
	return true;
}

GameObject* SceneSerializer::InstantiatePrefab(Scene& scene, AssetManager& assets, const std::string& relativePath,
	const DirectX::XMFLOAT3* position, const DirectX::XMFLOAT3* rotation)
{
	const std::wstring wideRelative = ToWide(relativePath);
	const std::wstring fullPath = Paths::GetAssetPath(wideRelative.c_str());
	uint64_t writeTime = 0;
	const std::shared_ptr<const json> document = LoadPrefabDocument(fullPath, writeTime);
	if (document == nullptr) return nullptr;

	// 메시·재질은 프리팹 파일(의 수정 버전)마다 씬에 한 번만 만들고 인스턴스끼리 공유함. 총알을 천 개 쏴도 재질이 천 벌이 되지 않음.
	// 키에 수정 시각을 넣어, 프리팹을 다시 저장하면 다음 인스턴스부터 새 버전의 자원을 씀 (이미 만든 인스턴스는 그대로).
	const std::string key = relativePath + "|" + std::to_string(writeTime);
	PrefabResources* resources = scene.FindPrefabResources(key);
	if (resources == nullptr)
	{
		PrefabResources created;
		std::vector<std::wstring> loadedModels;
		for (const json& j : document->value("meshes", json::array())) created.meshes.push_back(MeshFromJson(scene, assets, j, loadedModels));
		for (const json& j : document->value("materials", json::array())) created.materials.push_back(scene.AddMaterial(MaterialFromJson(j)));
		resources = &scene.StorePrefabResources(key, std::move(created));
	}

	std::vector<GameObject*> objects = CreateObjects(scene, document->value("objects", json::array()), false, resources->meshes, resources->materials);
	if (objects.empty()) return nullptr;
	GameObject* root = objects[0];
	root->SetPrefabSource(relativePath);
	if (position != nullptr) root->GetTransform().SetPosition(*position);
	if (rotation != nullptr) root->GetTransform().SetRotation(*rotation);
	return root;
}

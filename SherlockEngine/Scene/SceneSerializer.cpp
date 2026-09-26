#include "pch.h"
#include "Scene/SceneSerializer.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Graphics/Mesh.h"
#include "Graphics/Model.h"
#include "Core/AssetManager.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iterator>
#include <unordered_map>

using json = nlohmann::json;
using namespace DirectX;   // 이 파일 안에서만

namespace
{
	constexpr int kVersion = 2;   // 2: 11-C단계 컴포넌트 추가. 버전 1 파일도 그대로 읽힘 (components 없음)

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
		j["specularColor"] = ToJson(m.specularColor);
		j["shininess"] = m.shininess;
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
		m.specularColor = ToFloat3(j.value("specularColor", json()), m.specularColor);
		m.shininess = j.value("shininess", m.shininess);
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
		return components;
	}

	void ComponentsFromJson(GameObject& object, const json& components)
	{
		if (!components.is_array()) return;
		for (const json& c : components)
		{
			Behaviour* behaviour = object.AddBehaviour(c.value("type", ""));
			if (behaviour == nullptr) continue;   // 모르는 타입은 레지스트리가 경고를 남긴 뒤 건너뜀
			behaviour->enabled = c.value("enabled", true);
			JsonReadVisitor reader(c);
			behaviour->Reflect(reader);
		}
	}
}

std::string SceneSerializer::SaveToString(const Scene& scene, const Camera& camera)
{
	json root;
	root["version"] = kVersion;
	root["camera"] = { { "position", ToJson(camera.GetPosition()) }, { "yaw", camera.GetYaw() }, { "pitch", camera.GetPitch() } };
	root["ambientColor"] = ToJson(scene.ambientColor);
	root["clearColor"] = json::array({ scene.clearColor[0], scene.clearColor[1], scene.clearColor[2], scene.clearColor[3] });

	// 메시 출처. 인덱스 = 씬의 메시 순서.
	std::unordered_map<const Mesh*, int> meshIndex;
	json meshes = json::array();
	for (const auto& mesh : scene.GetMeshes())
	{
		const MeshSource* source = scene.GetMeshSource(mesh.get());
		json j;
		if (source == nullptr || source->type == MeshSource::Type::Custom)
		{
			j["type"] = "custom";
			Log::Warn("씬 저장: 출처를 모르는 메시(정점 %u)는 로드 시 비어 있게 된다.", mesh->GetVertexCount());
		}
		else
		{
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
		}
		meshIndex[mesh.get()] = static_cast<int>(meshes.size());
		meshes.push_back(j);
	}
	root["meshes"] = meshes;

	std::unordered_map<const Material*, int> materialIndex;
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

	json objects = json::array();
	for (const auto& objectPtr : scene.GetObjects())
	{
		const GameObject& object = *objectPtr;
		json j;
		j["name"] = object.GetName();
		auto mi = meshIndex.find(object.GetMesh());
		j["mesh"] = mi == meshIndex.end() ? -1 : mi->second;
		auto mati = materialIndex.find(object.GetMaterial());
		j["material"] = mati == materialIndex.end() ? -1 : mati->second;
		if (!object.GetSlotMaterials().empty())
		{
			json slots = json::array();
			for (const Material* slot : object.GetSlotMaterials())
			{
				auto si = materialIndex.find(slot);
				slots.push_back(si == materialIndex.end() ? -1 : si->second);
			}
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
		if (!object.GetBehaviours().empty()) j["components"] = ComponentsToJson(object);   // 11-C단계
		objects.push_back(j);
	}
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

	// ---- 메시. 모델 메시는 AssetManager 에서 가져옴 (이미지도 함께) ----
	std::vector<const Mesh*> meshes;
	std::vector<std::wstring> loadedModels;
	for (const json& j : root.value("meshes", json::array()))
	{
		MeshSource source;
		source.type = ParseSourceType(j.value("type", "custom"));
		const Mesh* created = nullptr;
		if (source.type == MeshSource::Type::Model)
		{
			source.modelPath = ToWide(j.value("path", ""));
			source.meshIndex = j.value("meshIndex", 0u);
			const Model* model = assets.GetModel(source.modelPath);
			if (model != nullptr && source.meshIndex < model->meshes.size())
			{
				Mesh copy = model->meshes[source.meshIndex];
				created = scene.AddMesh(std::move(copy), source);
				if (std::find(loadedModels.begin(), loadedModels.end(), source.modelPath) == loadedModels.end())
				{
					scene.AddModelImages(*model);
					loadedModels.push_back(source.modelPath);
				}
			}
			else
			{
				Log::Warn("씬 로드: 모델 메시를 찾지 못함 (%s #%u)", j.value("path", "").c_str(), source.meshIndex);
			}
		}
		else if (source.type != MeshSource::Type::Custom)
		{
			const json params = j.value("params", json::array());
			for (size_t i = 0; i < 4 && i < params.size(); ++i) source.params[i] = params[i].get<float>();
			source.a = j.value("a", 0u);
			source.b = j.value("b", 0u);
			source.color = ToFloat4(j.value("color", json()), source.color);
			Mesh mesh;
			if (source.CreatePrimitive(mesh)) created = scene.AddMesh(std::move(mesh), source);
		}
		else
		{
			Log::Warn("씬 로드: 출처 없는(custom) 메시는 비어 있다.");
		}
		meshes.push_back(created);   // nullptr 자리도 유지 (인덱스 보존)
	}

	std::vector<const Material*> materials;
	for (const json& j : root.value("materials", json::array()))
	{
		materials.push_back(scene.AddMaterial(MaterialFromJson(j)));
	}

	for (const json& j : root.value("lights", json::array()))
	{
		if (scene.GetLights().size() < MAX_LIGHTS) scene.GetLights().push_back(LightFromJson(j));
	}

	auto materialAt = [&materials](int index) -> const Material* { return (index >= 0 && index < static_cast<int>(materials.size())) ? materials[index] : nullptr; };
	for (const json& j : root.value("objects", json::array()))
	{
		const int mi = j.value("mesh", -1);
		const Mesh* mesh = (mi >= 0 && mi < static_cast<int>(meshes.size())) ? meshes[mi] : nullptr;
		const std::string name = j.value("name", "");
		GameObject& object = scene.AddObject(mesh, materialAt(j.value("material", -1)), name.c_str());
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
	}

	scene.ambientColor = ToFloat3(root.value("ambientColor", json()), scene.ambientColor);
	const json clear = root.value("clearColor", json());
	if (clear.is_array() && clear.size() >= 4) for (int i = 0; i < 4; ++i) scene.clearColor[i] = clear[i].get<float>();

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

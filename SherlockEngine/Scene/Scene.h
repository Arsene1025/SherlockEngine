#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <DirectXMath.h>
#include "Scene/GameObject.h"
#include "Scene/CameraComponent.h"
#include "Scene/Light.h"
#include "Scene/Material.h"

class Mesh;
struct Model;

// 11단계: 메시의 출처 정보. 씬을 저장할 때 정점 데이터 대신 이 정보를 기록하고, 로드할 때 이를 바탕으로 메시를 다시 만듦.
struct MeshSource
{
	enum class Type : uint8_t { Custom, Sphere, Cube, Plane, Cylinder, Model };
	Type type = Type::Custom;
	float params[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // Sphere: radius / Cube: size / Plane: width, depth / Cylinder: bottom, top, height
	uint32_t a = 0;                                  // Sphere: slices / Plane: m / Cylinder: slices
	uint32_t b = 0;                                  // Sphere: stacks / Plane: n / Cylinder: stacks
	DirectX::XMFLOAT4 color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	std::wstring modelPath;                          // Model: Assets 기준 상대 경로
	uint32_t meshIndex = 0;                          // Model: 모델 안의 메시 번호

	static MeshSource Sphere(float radius, uint32_t slices, uint32_t stacks, const DirectX::XMFLOAT4& color);
	static MeshSource Cube(float size, const DirectX::XMFLOAT4& color);
	static MeshSource Plane(float width, float depth, uint32_t m, uint32_t n, const DirectX::XMFLOAT4& color);
	static MeshSource Cylinder(float bottom, float top, float height, uint32_t slices, uint32_t stacks, const DirectX::XMFLOAT4& color);
	static MeshSource FromModel(const std::wstring& path, uint32_t meshIndex);
	// 프리미티브 출처 정보로 메시를 만듦. Model/Custom 이면 false 를 반환함.
	bool CreatePrimitive(Mesh& out) const;
};

// 씬 = 그릴 대상들의 CPU 쪽 데이터.
//
// 오브젝트·조명·재질 목록과 환경(주변광·클리어 색)을 담음. GPU 자원은 갖지 않음.
// Renderer는 Render(const Scene&, const Camera&)로 씬을 입력받아 그리기만 함.
// 예전에는 Renderer가 구 하나, 조명 배열, ImGui 패널을 직접 들고 있었음(D3).
//
// 메시와 재질은 unique_ptr로 소유해 포인터(주소)가 바뀌지 않게 함. GameObject가 그
// 포인터를 들고 있고, Renderer의 GPU 캐시도 그 포인터를 키로 사용함.
//
// 9단계: 모델(Model)의 메시·재질·이미지를 통째로 받음. 이미지는 인코딩된 바이트 상태로 이름으로 찾을 수 있게
// 보관하고, Renderer 의 텍스처 캐시가 재질 이름 → 씬 이미지 → 디코딩 순으로 텍스처를 만듦.
class Scene
{
public:
	Scene();
	~Scene();

	Mesh* AddMesh(Mesh&& mesh);
	Mesh* AddMesh(Mesh&& mesh, const MeshSource& source);   // 11단계: 출처 정보와 함께 추가 (저장 가능)
	const MeshSource* GetMeshSource(const Mesh* mesh) const;
	// 모델의 이미지 바이트만 씬에 넣음 (씬 로드 시 재질은 JSON 에서, 이미지는 모델에서 가져옴).
	void AddModelImages(const Model& model);
	Material* AddMaterial(const Material& material);
	GameObject& AddObject(const Mesh* mesh, const Material* material, const char* name);

	// 모델의 메시·재질·이미지를 이 씬으로 옮기고(모델은 비워짐) 인스턴스마다 GameObject 를 만듦.
	// 만든 오브젝트 수를 돌려줌. transform 은 모든 인스턴스에 똑같이 적용됨(모델 전체를 옮길 때 사용).
	size_t AddModel(Model&& model, const Transform& transform);
	// 10단계: AssetManager 캐시의 모델을 복사해 넣음 (원본은 그대로 남아 다음 전환 때 다시 쓰임).
	size_t AddModel(const Model& model, const Transform& transform);

	// 모든 오브젝트·메시·재질·이미지·조명을 버림. Renderer 캐시가 이 포인터들을 키로 쓰므로
	// 먼저 Renderer::InvalidateScene 을 호출해야 함.
	void Clear();

	// 11-C단계: GameObject 를 unique_ptr 로 보관함 — 벡터가 늘어나도 주소가 바뀌지 않아 컴포넌트가 소유자를 가리킬 수 있음.
	// 에디터의 선택 대상은 여전히 인덱스로 식별함 (RemoveObject 하면 뒤쪽 오브젝트의 인덱스가 하나씩 앞당겨짐).
	std::vector<std::unique_ptr<GameObject>>& GetObjects() { return m_objects; }
	const std::vector<std::unique_ptr<GameObject>>& GetObjects() const { return m_objects; }
	GameObject* GetObject(size_t index) { return index < m_objects.size() ? m_objects[index].get() : nullptr; }
	GameObject* FindObject(const std::string& name);
	void RemoveObject(size_t index);

	// ---- 11-C단계: 재생 (에디터의 ▶). 재생 중에만 컴포넌트의 Start/Update/FixedUpdate 가 호출됨 ----
	void BeginPlay(Input* input, Camera* camera);
	void Update(float dt);          // Start 를 아직 받지 않은 컴포넌트(재생 중 추가된 것 포함)는 Start 를 먼저 호출함
	void FixedUpdate(float fixedDt);
	void EndPlay();
	bool IsPlaying() const { return m_playing; }
	PlayContext& GetPlayContext() { return m_playContext; }

	// 11-C단계: 씬 안의 카메라. enabled 인 CameraComponent 중 priority 가 가장 높은 것이 활성 카메라가 되어 재생 중 매 프레임 엔진 Camera 에
	// 자세를 기록함 (Update 끝에서). 활성 카메라가 바뀌면 새 카메라의 blendTime 동안 이전 시점에서 보간함. 카메라가 없으면 에디터 카메라를 그대로 씀.
	CameraComponent* FindActiveCamera();
	CameraComponent* GetActiveCamera() const { return m_activeCamera; }

	std::vector<std::unique_ptr<Mesh>>& GetMeshes() { return m_meshes; }
	const std::vector<std::unique_ptr<Mesh>>& GetMeshes() const { return m_meshes; }

	std::vector<std::unique_ptr<Material>>& GetMaterials() { return m_materials; }
	const std::vector<std::unique_ptr<Material>>& GetMaterials() const { return m_materials; }

	// 이름 → 인코딩된 이미지 바이트. 없으면 nullptr.
	void AddImage(const std::string& name, std::vector<uint8_t>&& encoded);
	const std::vector<uint8_t>* FindImage(const std::string& name) const;
	size_t GetImageCount() const { return m_images.size(); }

	// 최대 MAX_LIGHTS개. 넘치는 조명은 Renderer가 잘라냄.
	std::vector<LightData>& GetLights() { return m_lights; }
	const std::vector<LightData>& GetLights() const { return m_lights; }

	DirectX::XMFLOAT3 ambientColor = DirectX::XMFLOAT3(0.12f, 0.12f, 0.12f);
	float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };

private:
	std::vector<std::unique_ptr<Mesh>> m_meshes;
	std::vector<MeshSource> m_meshSources;   // m_meshes 와 같은 인덱스
	std::vector<std::unique_ptr<Material>> m_materials;
	std::vector<std::unique_ptr<GameObject>> m_objects;
	std::vector<LightData> m_lights;
	std::unordered_map<std::string, std::vector<uint8_t>> m_images;

	// 11-C단계
	void ApplyActiveCamera(float dt);
	bool m_playing = false;
	PlayContext m_playContext;
	CameraComponent* m_activeCamera = nullptr;
	CameraPose m_blendFrom;          // 전환 시작 시점의 카메라 자세
	float m_blendElapsed = 0.0f;
	float m_blendDuration = 0.0f;
};

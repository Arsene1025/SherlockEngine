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

// 11단계: 메시의 출처. 씬을 저장할 때 정점 데이터 대신 이것을 적고, 로드할 때 다시 만든다.
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
	// 프리미티브 출처로 메시를 만든다. Model/Custom 이면 false.
	bool CreatePrimitive(Mesh& out) const;
};

// 씬 = 그릴 것들의 CPU 쪽 데이터.
//
// 오브젝트 목록, 조명 목록, 재질 목록, 환경(주변광·클리어 색). GPU 자원은 없다.
// Renderer는 Render(const Scene&, const Camera&)로 이것을 입력으로 받아 그리기만 한다.
// 예전에는 Renderer가 구 하나와 조명 배열과 ImGui 패널을 직접 들고 있었다(D3).
//
// 메시와 재질은 unique_ptr로 소유해 포인터가 흔들리지 않게 한다. GameObject가 그
// 포인터를 들고, Renderer의 GPU 캐시도 그 포인터를 키로 쓴다.
//
// 9단계: 모델(Model)의 메시·재질·이미지를 통째로 받는다. 이미지는 인코딩된 바이트를 이름으로 찾을 수 있게
// 들고 있고, Renderer 의 텍스처 캐시가 재질 이름 → 씬 이미지 → 디코딩 순으로 푼다.
class Scene
{
public:
	Scene();
	~Scene();

	Mesh* AddMesh(Mesh&& mesh);
	Mesh* AddMesh(Mesh&& mesh, const MeshSource& source);   // 11단계: 출처와 함께 (저장 가능)
	const MeshSource* GetMeshSource(const Mesh* mesh) const;
	// 모델의 이미지 바이트만 씬에 넣는다 (로드 시 재질은 JSON 에서, 이미지는 모델에서).
	void AddModelImages(const Model& model);
	Material* AddMaterial(const Material& material);
	GameObject& AddObject(const Mesh* mesh, const Material* material, const char* name);

	// 모델의 메시·재질·이미지를 이 씬으로 옮기고(모델은 비워진다) 인스턴스마다 GameObject 를 만든다.
	// 만든 오브젝트 수를 돌려준다. transform 은 모든 인스턴스에 같이 적용된다(모델 전체를 옮길 때).
	size_t AddModel(Model&& model, const Transform& transform);
	// 10단계: AssetManager 캐시의 모델을 복사해 넣는다 (원본은 그대로 남아 다음 전환에 다시 쓴다).
	size_t AddModel(const Model& model, const Transform& transform);

	// 모든 오브젝트·메시·재질·이미지·조명을 버린다. Renderer 캐시가 이 포인터들을 키로 쓰므로
	// 먼저 Renderer::InvalidateScene 을 불러야 한다.
	void Clear();

	// 11-C단계: GameObject 는 unique_ptr — 벡터가 늘어도 주소가 안정적이라 컴포넌트가 소유자를 가리킬 수 있다.
	// 인덱스는 여전히 에디터 선택의 식별자다 (RemoveObject 가 뒤 인덱스를 밀어낸다).
	std::vector<std::unique_ptr<GameObject>>& GetObjects() { return m_objects; }
	const std::vector<std::unique_ptr<GameObject>>& GetObjects() const { return m_objects; }
	GameObject* GetObject(size_t index) { return index < m_objects.size() ? m_objects[index].get() : nullptr; }
	GameObject* FindObject(const std::string& name);
	void RemoveObject(size_t index);

	// ---- 11-C단계: 재생 (에디터의 ▶). 재생 중에만 컴포넌트의 Start/Update/FixedUpdate 가 돈다 ----
	void BeginPlay(Input* input, Camera* camera);
	void Update(float dt);          // Start 를 아직 안 받은 컴포넌트(재생 중 추가 포함)는 먼저 Start
	void FixedUpdate(float fixedDt);
	void EndPlay();
	bool IsPlaying() const { return m_playing; }
	PlayContext& GetPlayContext() { return m_playContext; }

	// 11-C단계: 씬 안의 카메라. priority 가 가장 높은 enabled CameraComponent 가 활성이고 재생 중 매 프레임 엔진 Camera 에
	// 자세를 쓴다 (Update 끝에서). 카메라가 바뀌면 새 카메라의 blendTime 동안 이전 시점에서 보간한다. 없으면 에디터 카메라 그대로.
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

	// 최대 MAX_LIGHTS개. 넘치는 것은 Renderer가 잘라낸다.
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

#pragma once
#include "Core/EngineApi.h"   // 2026-10-08: SHERLOCK_API (엔진 DLL 내보내기)
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <DirectXMath.h>
#include "Scene/GameObject.h"
#include "Scene/CameraComponent.h"
#include "Scene/Light.h"
#include "Scene/Material.h"
#include "Scene/PostProcessSettings.h"

class Mesh;
struct Model;
class AssetManager;

// 2026-10-15: 프리팹 파일 하나가 씬에 만든 메시·재질. 같은 프리팹의 인스턴스는 이것을 공유함 (Scene::Instantiate).
struct PrefabResources
{
	std::vector<const Mesh*> meshes;
	std::vector<const Material*> materials;
};

// 메시의 출처 정보. 씬을 저장할 때 정점 데이터 대신 이 정보를 기록하고, 로드할 때 이를 바탕으로 메시를 다시 만듦.
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

// 2026-10-08: 씬의 환경 — 배경(스카이박스)과 환경광(IBL)과 출력(노출·톤매핑).
//
// 큐브맵·LUT 는 재질 텍스처처럼 핸들이 아니라 이름으로 참조하고, 이름 → GPU 텍스처 변환은 Renderer 가 함.
//   - 빈 문자열: 없음
//   - 내장: "builtin:sky" (절차적 하늘 그라데이션), "builtin:skyfaces" (면마다 다른 색 — 방향 검증용)
//   - 파일: Assets/Textures/ 기준 상대 경로, 또는 "asset:<Assets 기준 상대 경로>". 큐브는 큐브 DDS 나 면 이미지 6장이 든 폴더,
//     LUT 는 2D 이미지(보통 부동소수점 DDS)
// 씬 전체에 하나뿐이고 트랜스폼이 없으므로 GameObject 가 아니라 주변광과 같은 값으로 둠.
//
// IBL 맵이 비어 있으면: 스카이박스가 있으면 그 큐브를 밉으로 흐려 확산·반사에 대신 쓰고(근사), 스카이박스도 없으면 ambientColor 를 씀.
struct SceneEnvironment
{
	std::string skybox;        // 배경 큐브맵 (보통 HDR 원본)
	std::string irradiance;    // IBL 확산: 노멀 방향의 코사인 가중 평균 복사휘도 큐브
	std::string specular;      // IBL 반사: 거칠기별로 미리 거른 큐브. 밉 0 = 거칠기 0, 마지막 밉 = 거칠기 1
	std::string brdfLut;       // split-sum BRDF LUT (2D, u = N·V, v = 1 − roughness). 비면 해석적 근사
	float iblIntensity = 1.0f; // 환경광 배율 (IBL 맵이나 스카이박스를 쓸 때만. ambientColor 대체 경로에는 적용 안 함)
	float exposure = 1.0f;     // 최종 색(물체·하늘)에 곱함
	bool toneMapping = false;  // ACES 근사. HDR 환경에서 켬. 끄면 1 을 넘는 값이 잘림
	PostProcessSettings post;  // 2026/10/23: 후처리 (블러·블룸·GreyScale). 환경에 두어 저장·되돌리기가 환경과 함께 됨
};

// 씬 = 그릴 대상들의 CPU 쪽 데이터.
//
// 오브젝트·조명·재질 목록과 환경(주변광·클리어 색·SceneEnvironment)을 담음. GPU 자원은 갖지 않음.
// Renderer는 Render(const Scene&, const Camera&)로 씬을 입력받아 그리기만 함.
// 예전에는 Renderer가 구 하나, 조명 배열, ImGui 패널을 직접 들고 있었음(D3).
//
// 메시와 재질은 unique_ptr로 소유해 포인터(주소)가 바뀌지 않게 함. GameObject가 그
// 포인터를 들고 있고, Renderer의 GPU 캐시도 그 포인터를 키로 사용함.
//
// 9단계: 모델(Model)의 메시·재질·이미지를 통째로 받음. 이미지는 인코딩된 바이트 상태로 이름으로 찾을 수 있게
// 보관하고, Renderer 의 텍스처 캐시가 재질 이름 → 씬 이미지 → 디코딩 순으로 텍스처를 만듦.
class SHERLOCK_API Scene
{
public:
	Scene();
	~Scene();
	// 2026-10-08: 복사 금지를 명시함. 엔진 DLL 이 클래스를 통째로 내보내면(SHERLOCK_API) 암묵적 복사 생성자까지 인스턴스화되는데,
	// unique_ptr 벡터 멤버 때문에 그 복사 생성자는 컴파일되지 않음.
	Scene(const Scene&) = delete;
	Scene& operator=(const Scene&) = delete;

	Mesh* AddMesh(Mesh&& mesh);
	Mesh* AddMesh(Mesh&& mesh, const MeshSource& source);   // 11단계: 출처 정보와 함께 추가 (저장 가능)
	const MeshSource* GetMeshSource(const Mesh* mesh) const;
	// 모델의 이미지 바이트만 씬에 넣음 (씬 로드 시 재질은 JSON 에서, 이미지는 모델에서 가져옴).
	void AddModelImages(const Model& model);
	Material* AddMaterial(const Material& material);
	GameObject& AddObject(const Mesh* mesh, const Material* material, const char* name);

	// 모델의 메시·재질·이미지를 이 씬으로 옮기고(모델은 비워짐) 인스턴스마다 GameObject 를 만듦.
	// 만든 오브젝트 수를 돌려줌. transform 은 모델 전체의 배치임.
	// 11-F단계: 노드가 둘 이상이면 빈 루트 오브젝트(파일 이름)를 먼저 만들고 인스턴스를 그 자식으로 둠 — 반환값에 루트가 포함됨.
	// 루트가 transform 을 갖고 자식은 pre(노드 행렬)만 가지므로 월드 행렬은 이전 방식(pre × transform)과 같음.
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
	// 2026-10-15: 안정 ID 로 찾음. 없으면(지워졌거나 모르는 ID) nullptr — 오브젝트 포인터를 오래 들고 있는 대신 ID 를 들고 이것으로 다시 찾음.
	GameObject* FindById(uint64_t id);
	const GameObject* FindById(uint64_t id) const;
	// 직렬화가 읽은 ID 보다 큰 값부터 새 ID 가 나가도록 카운터를 올림 (SceneSerializer 가 오브젝트를 만들기 전에 호출함).
	void ReserveIds(uint64_t maxUsed) { if (m_nextId <= maxUsed) m_nextId = maxUsed + 1; }
	size_t IndexOf(const GameObject* object) const;   // 11-F단계: 포인터 → 벡터 인덱스. 없으면 SIZE_MAX
	void RemoveObject(size_t index);                  // 11-F단계: 자식도 함께 지움 (즉시. 재생 중 게임 코드는 Destroy 를 쓸 것)
	void ClearObjects();                              // 2026-10-15: 오브젝트만 비움 (메시·재질·조명·환경은 유지). 되돌리기 복원이 씀. ID 카운터는 그대로

	// ---- 2026-10-15: 생성(Instantiate)과 지연 파괴(Destroy) — 유니티의 Instantiate / Destroy ----
	// 에셋 관리자 (프리팹의 모델 메시를 읽는 데 필요함). Engine 이 초기화할 때 한 번 지정함. 없으면 프리팹 Instantiate 는 실패함.
	void SetAssets(AssetManager* assets) { m_assets = assets; }
	// 프리팹(Assets 기준 상대 경로, 예: "Prefabs\\Bullet.prefab")을 씬에 만들고 루트를 돌려줌. 실패하면 nullptr.
	// position·rotation(라디안)은 루트의 로컬 자세. 자세를 주지 않으면 프리팹에 저장된 값 그대로임.
	// 재생 중 컴포넌트의 Update 안에서 불러도 안전함: 새 오브젝트는 목록 끝에 붙고, 같은 프레임 순회가 거기까지 오면 Start 와 첫 Update 를 받음.
	GameObject* Instantiate(const std::string& prefabPath);
	GameObject* Instantiate(const std::string& prefabPath, const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
	// 씬의 오브젝트(와 자손)를 복제함 (메시·재질 공유, 이름 뒤에 " (Clone)"). 에디터의 Duplicate 도 이것을 씀.
	GameObject* Instantiate(const GameObject& source);
	// 오브젝트(와 자손)를 지우도록 표시함. 재생 중에는 지금 도는 Update/FixedUpdate 가 끝난 뒤(프레임 끝)에 지워지고, 그때까지 그 오브젝트는
	// 남은 Update 를 받지 않음. 재생 중이 아니면 순회하는 코드가 없으므로 바로 지움. 지운 뒤에는 포인터가 무효이므로 오래 들고 있을 오브젝트는
	// GetId() 를 저장해 두고 FindById 로 다시 찾을 것 (없으면 이미 지워진 것).
	void Destroy(GameObject* object);
	// 프리팹 자원 캐시 (SceneSerializer::InstantiatePrefab 이 씀). 씬을 비우면 함께 비워짐.
	PrefabResources* FindPrefabResources(const std::string& key);
	PrefabResources& StorePrefabResources(const std::string& key, PrefabResources&& resources);

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

	// 2026-10-08: 스카이박스와 IBL (SceneEnvironment). 주변광과 같은 "환경" 값.
	SceneEnvironment environment;

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
	CameraPose m_blendFrom;          // 전환 시작 시점의 카메라 자세 (11-F단계: 회전은 쿼터니언, slerp 로 보간)
	float m_blendElapsed = 0.0f;
	float m_blendDuration = 0.0f;

	// 2026-10-15: 멤버는 끝에만 덧붙임 (이미 빌드된 스크립트 DLL 과의 ABI 호환)
	uint64_t m_nextId = 1;           // 다음에 부여할 오브젝트 ID. Clear 에서만 1 로 돌아감
	AssetManager* m_assets = nullptr;
	std::unordered_map<std::string, PrefabResources> m_prefabResources;
	bool m_destroyPending = false;   // Destroy 로 표시된 오브젝트가 있음 → 프레임 끝에 FlushDestroyed

	// 2026-10-15: 투영 선택. 재생 시작 때의 렌즈(투영 방식 포함)를 기억했다가 정지에서 되돌림 — 카메라 컴포넌트가 직교였으면 에디터 카메라가 직교로 남지 않게.
	struct SavedLens
	{
		bool valid = false;
		bool perspective = true;
		float fovY = 0.0f;
		float orthoHeight = 25.0f;
		float nearZ = 0.1f;
		float farZ = 1000.0f;
	};
	SavedLens m_savedLens;
	float m_blendFromOrthoHeight = 25.0f;   // 전환 시작 시점의 직교 크기 (보간용)
	void FlushDestroyed();
};

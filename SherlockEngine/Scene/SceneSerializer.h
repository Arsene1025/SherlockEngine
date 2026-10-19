#pragma once
#include <string>
#include <DirectXMath.h>

class Scene;
class Camera;
class AssetManager;
class GameObject;

// 씬 저장/로드 (11단계). 형식은 JSON (nlohmann-json).
//
// 저장하는 것: 카메라, 주변광·클리어 색, 조명, 재질(전 필드), 메시 출처(프리미티브 인자 또는 모델 경로 + 인덱스),
// 오브젝트(이름, 메시·재질 인덱스, 슬롯 재질, Transform, 노드 pre-transform).
// 저장하지 않는 것: GPU 자원(핸들은 저장할 수 있는 값이 아님), 정점 데이터(출처 정보로 다시 만듦), 렌더 설정(Renderer 의 몫).
// 모델 메시는 AssetManager 로 다시 읽어 그 메시와 이미지 바이트를 씬에 넣음. 재질은 JSON 값이 우선함
// (편집한 값이 유지됨) — 이미지 이름은 9단계 규칙("<모델>/<번호> <uri>")을 따르므로 다시 읽은 모델의 이미지와 일치함.
// 11-C단계: 오브젝트마다 컴포넌트의 Reflect 필드를 "components": [{"type":"Orbit", "enabled":true, "radius":8, ...}] 형태로 저장함.
// 문자열 버전(SaveToString/LoadFromString)은 에디터의 ▶ Play 스냅샷에서 사용함: 재생 전 씬을 메모리에 기록해 두었다가 ■ Stop 때 되돌림.
namespace SceneSerializer
{
	bool Save(const Scene& scene, const Camera& camera, const std::wstring& path);
	// 씬을 비운 뒤 파일 내용으로 채움. 호출 전에 Renderer::InvalidateScene 을 호출할 것 (Renderer 캐시가 포인터를 키로 쓰기 때문).
	bool Load(Scene& scene, Camera& camera, AssetManager& assets, const std::wstring& path);

	std::string SaveToString(const Scene& scene, const Camera& camera);
	bool LoadFromString(Scene& scene, Camera& camera, AssetManager& assets, const std::string& text, const char* label);

	// ---- 2026-10-15: 편집 상태 (에디터의 되돌리기 이력) ----
	// 오브젝트(자식·컴포넌트·안정 ID 포함), 재질 내용, 조명, 환경·주변광·클리어 색을 한 문자열로 만듦. 메시·이미지·카메라는 넣지 않음 —
	// 편집 중에는 메시·재질이 늘기만 하므로 인덱스로 가리키면 충분하고, 그래서 Renderer 캐시를 건드리지 않고 되돌릴 수 있음.
	std::string SaveEditState(const Scene& scene);
	// 오브젝트를 모두 지우고 문자열에서 다시 만듦. 재질은 같은 인덱스의 것을 제자리에서 덮어씀. 메시·재질 목록은 그대로임.
	// 따라서 문자열을 만든 뒤 Scene::Clear 가 있었다면 쓰면 안 됨 (호출자가 이력을 비움). 오브젝트 포인터는 모두 바뀜.
	bool RestoreEditState(Scene& scene, const std::string& state);

	// ---- 2026-10-15: 서브트리 복제와 프리팹 ----
	// source 와 그 자손을 같은 씬 안에 복제함 (메시·재질 공유, 새 ID, 이름 뒤에 " (Clone)"). 복제본의 루트를 돌려줌.
	GameObject* CloneSubtree(Scene& scene, const GameObject& source);
	// root 와 그 자손을 프리팹 파일로 저장함. 파일은 메시 출처와 재질을 자체로 지님 (씬 없이도 읽힘). 형식은 SceneSerializer.cpp 의 SavePrefab 참고.
	bool SavePrefab(const Scene& scene, const GameObject& root, const std::wstring& path);
	// 프리팹(Assets 기준 상대 경로)을 씬에 만들고 루트를 돌려줌. 실패하면 nullptr (로그에 이유).
	// position·rotation(라디안)을 주면 루트의 로컬 자세를 그 값으로 바꿈. 메시·재질은 프리팹마다 씬에 한 번만 만들어 인스턴스끼리 공유함.
	GameObject* InstantiatePrefab(Scene& scene, AssetManager& assets, const std::string& relativePath,
		const DirectX::XMFLOAT3* position = nullptr, const DirectX::XMFLOAT3* rotation = nullptr);
}

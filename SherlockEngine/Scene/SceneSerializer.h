#pragma once
#include <string>

class Scene;
class Camera;
class AssetManager;

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
}

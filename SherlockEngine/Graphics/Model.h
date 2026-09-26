#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <DirectXMath.h>
#include "Graphics/Mesh.h"
#include "Scene/Material.h"

// 9단계: 모델 = Mesh 배열 + Material 배열 + 인스턴스(노드) 배열 + 이미지 바이트.
//
// 전부 CPU 데이터라 Device 를 모름. Scene::AddModel 이 메시·재질·이미지를 씬으로 옮기고
// 인스턴스마다 GameObject 를 만듦. GPU 자원(버퍼·텍스처·ResourceSet)은 그 뒤 Renderer 캐시가
// 처음 그릴 때 만듦 — 3~5단계와 같은 경계임.
//
// 좌표계: glTF 는 오른손(+Y 위, −Z 앞, CCW 앞면), 엔진은 왼손(CW 앞면). 로더가 Z 를 뒤집어서 읽어 들임.
// 뒤집기(반사)가 와인딩을 CCW → CW 로 바꾸므로 인덱스 순서는 그대로 둠. 탄젠트 w 는 부호가 뒤집힘.
//
// 이미지: 파일을 여기서 디코딩하지 않음. 인코딩된 바이트(PNG/JPG)를 이름과 함께 들고 있고, Renderer 의
// 텍스처 캐시가 재질이 요구하는 색공간(알베도 sRGB, 노멀 선형)으로 디코딩함. 같은 이미지를 두
// 색공간으로 쓰는 재질이 있어도 원본 바이트 하나로 충분함.
struct ModelImage
{
	std::string name;               // Material 이 참조하는 이름. "<모델>/<번호> <uri>"
	std::vector<uint8_t> encoded;   // PNG/JPG/DDS 파일 내용 그대로
};

struct ModelInstance
{
	uint32_t meshIndex = 0;
	DirectX::XMFLOAT4X4 world;      // 노드의 월드 변환 (엔진 좌표계, 행벡터 관례)
	std::string name;
};

struct ModelStats
{
	uint32_t meshes = 0;
	uint32_t submeshes = 0;
	uint32_t instances = 0;
	uint32_t materials = 0;
	uint32_t images = 0;
	uint32_t vertices = 0;
	uint32_t triangles = 0;
	uint32_t meshesWith16BitIndices = 0;   // 정점이 65,536개 미만이라 Renderer 가 16비트 인덱스 버퍼를 쓰는 메시 수
	uint32_t generatedTangentMeshes = 0;   // TANGENT 속성이 없어 로더가 만든 메시 수
	uint32_t skippedPrimitives = 0;        // 삼각형이 아니거나 POSITION 이 없는 primitive
	double loadMilliseconds = 0.0;
};

struct Model
{
	std::string name;
	std::wstring sourcePath;            // 10~11단계: Assets 기준 상대 경로 (AssetManager 가 채움). 씬 저장 시 메시 출처로 쓰임
	std::vector<Mesh> meshes;
	std::vector<Material> materials;    // materialSlot → 이 배열의 인덱스
	std::vector<ModelImage> images;
	std::vector<ModelInstance> instances;
	Bounds bounds;                      // 모든 인스턴스를 월드로 옮긴 뒤의 합집합
	ModelStats stats;

	bool IsValid() const { return !meshes.empty() && !instances.empty(); }
};

namespace ModelLoader
{
	struct Options
	{
		float scale = 1.0f;                 // 균일 스케일 (루트에 곱함)
		bool generateMissingTangents = true;
		bool flipZ = true;                  // 오른손 → 왼손. 끄면 glTF 좌표 그대로(거울상으로 보임)
	};

	// glTF 2.0 (.gltf + .bin + 이미지 파일, 또는 .glb). 성공하면 true. 오류·경고는 Log 로 남김.
	bool LoadGltf(const std::wstring& path, const Options& options, Model& out);
}

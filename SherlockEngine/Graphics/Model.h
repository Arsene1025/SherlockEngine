#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <DirectXMath.h>
#include "Core/EngineApi.h"
#include "Graphics/Mesh.h"
#include "Scene/Material.h"

// 모델 = Mesh 배열 + Material 배열 + 인스턴스(노드) 배열 + 이미지 바이트.
//
// 전부 CPU 데이터라 Device 를 모름. Scene::AddModel 이 메시·이미지를 씬과 함께 소유하게 하고(복사 없음)
// 재질을 복사한 뒤 인스턴스마다 GameObject 를 만듦. GPU 자원(버퍼·텍스처·ResourceSet)은 그 뒤 Renderer 캐시가
// 처음 그릴 때 만듦 — Mesh·Material 과 같은 경계임.
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
	// PNG/JPG/DDS 파일 내용 그대로. shared_ptr: 씬에 놓을 때 바이트를 복사하지 않고 씬과 함께 소유함
	std::shared_ptr<const std::vector<uint8_t>> encoded;
};

struct ModelInstance
{
	uint32_t meshIndex = 0;
	DirectX::XMFLOAT4X4 world;      // 노드의 월드 변환 (엔진 좌표계, 행벡터 관례)
	std::string name;
	int node = -1;                  // Model::nodes 인덱스 (glTF nodes[] 번호)
	int skin = -1;                  // Model::skins 인덱스. 스킨이 없으면 -1
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
	uint32_t skinnedMeshes = 0;            // 조인트·가중치를 가진 메시 수
	uint32_t generatedTangentMeshes = 0;   // TANGENT 속성이 없어 로더가 만든 메시 수
	uint32_t skippedPrimitives = 0;        // 삼각형이 아니거나 POSITION 이 없는 primitive
	double loadMilliseconds = 0.0;
};

// glTF nodes[] 와 같은 번호의 노드 표. 애니메이션 채널이 노드 번호로 가리킴.
// 로컬 변환은 엔진 좌표계(flipZ 반영)의 이동·회전(쿼터니언 xyzw)·스케일이고, options.scale 은 반영하지 않음
// (그 값은 월드 변환 끝에 곱해짐). 월드 = S·R·T 로 만든 로컬을 자식 → 부모 순으로 곱한 것.
struct ModelNode
{
	std::string name;
	int parent = -1;                                                  // 부모 노드 번호. 루트면 -1
	int meshIndex = -1;                                               // Model::meshes 인덱스. 메시가 없거나 건너뛴 메시면 -1
	int skin = -1;                                                    // Model::skins 인덱스. 스킨이 없으면 -1
	DirectX::XMFLOAT3 translation = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	DirectX::XMFLOAT4 rotation = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	DirectX::XMFLOAT3 scale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
};

// glTF skins[] 하나. 조인트 = 노드 번호 목록이고, 스키닝 메시의 JOINTS_0 번호가 이 목록의 위치를 가리킴.
// inverseBind[i] 는 i 번째 조인트의 역바인드 행렬 (메시 정점 공간 → 조인트 공간). 엔진 좌표계(flipZ 반영), 행벡터 관례.
// 없으면 단위행렬로 채워 joints 와 같은 길이를 유지함.
//
// 아래 세 필드는 임포트 때 한 번 만들어 두는 계층 정보 (Model 은 만든 뒤 바뀌지 않으므로 인스턴스끼리 공유함).
// 노드 번호는 channel.node·joints 가 가리키므로 재배열하지 않고, 순서만 따로 저장함.
struct ModelSkin
{
	std::string name;
	std::vector<uint32_t> joints;
	std::vector<DirectX::XMFLOAT4X4> inverseBind;

	// 계산해야 할 노드 번호: 조인트와 그 조상 노드. 부모가 항상 자식보다 앞에 있어서 앞에서부터 한 번 훑으면 월드 행렬이 구해짐.
	std::vector<uint32_t> evalOrder;
	// jointParent[j] = 조인트 j 의 부모 조인트 번호 (joints 의 위치). 부모 노드가 조인트가 아니거나 루트면 -1
	std::vector<int> jointParent;
	// jointParent 가 -1 인 조인트 번호들 (루트 본). 스킨에 따라 하나일 수도 여럿일 수도 있음
	std::vector<uint32_t> rootJoints;
};

enum class AnimationPath : uint8_t { Translation, Rotation, Scale, Weights };
enum class AnimationInterpolation : uint8_t { Linear, Step, CubicSpline };

// 채널 하나 = 노드 하나의 속성 하나를 시간에 따라 바꾸는 키 배열.
// values 는 키마다 성분 수만큼 이어 붙임: Translation·Scale 3, Rotation 4(xyzw), Weights 는 모프 타깃 수.
// CubicSpline 이면 키마다 [입력 탄젠트, 값, 출력 탄젠트] 순서로 3배가 됨. 값은 엔진 좌표계(flipZ 반영)로 바꾼 뒤의 것.
struct AnimationChannel
{
	uint32_t node = 0;
	AnimationPath path = AnimationPath::Translation;
	AnimationInterpolation interpolation = AnimationInterpolation::Linear;
	std::vector<float> times;     // 초. 증가하는 순서
	std::vector<float> values;
};

struct AnimationClip
{
	std::string name;
	float duration = 0.0f;        // 모든 채널의 마지막 키 시간 중 최댓값
	std::vector<AnimationChannel> channels;
};

struct Model
{
	std::string name;
	std::wstring sourcePath;            // Assets 기준 상대 경로 (AssetManager 가 채움). 씬 저장 시 메시 출처로 쓰임
	// 만든 뒤로 바뀌지 않음 (const). shared_ptr: Scene::AddModel 이 복사하지 않고 함께 소유함
	std::vector<std::shared_ptr<const Mesh>> meshes;
	std::vector<Material> materials;    // materialSlot → 이 배열의 인덱스
	std::vector<ModelImage> images;
	std::vector<ModelInstance> instances;
	std::vector<ModelNode> nodes;
	std::vector<ModelSkin> skins;
	std::vector<AnimationClip> animations;
	Bounds bounds;                     // 모든 인스턴스를 월드로 옮긴 뒤의 합집합
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
	// 구현은 Graphics/Import/GltfImporter.cpp (nlohmann-json 으로 직접 파싱).
	SHERLOCK_API bool LoadGltf(const std::wstring& path, const Options& options, Model& out);
}

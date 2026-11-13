#pragma once
#include <vector>
#include <DirectXMath.h>
#include "Scene/GameObject.h"   // GameObject::SkeletonPose

class Scene;
struct Model;
struct ModelNode;
struct ModelSkin;

// 스킨 포즈 계산: 노드 TRS 표 → 조인트 월드 → (스킨 팔레트, 본 디버그용 스켈레톤 포즈).
// 재생 중에는 Animator 가 프레임마다, 편집 모드에서는 ApplyRestSkinPoses 가 기본 자세(레스트 포즈)로 같은 식을 씀.

// 기본 자세 노드 표에서 노드 n 의 월드 (재생과 무관, Start 에서 한 번)
DirectX::XMMATRIX ComputeNodeGlobal(const std::vector<ModelNode>& nodes, int n);

// 모델 안에서 meshIndex 번 메시를 그리는 스킨 인스턴스를 찾음. 조건에 맞는 첫 번째 것을 고르고 (Animator::Start 와
// ApplyRestSkinPoses 가 같은 인스턴스를 고르도록 한 함수에 둠), 그 인스턴스의 메시 노드 번호를 meshNode 로 돌려줌. 없으면 nullptr.
const ModelSkin* FindMeshSkin(const Model& model, uint32_t meshIndex, int& meshNode);

// 두 단계로 나뉨. 같은 스킨을 쓰는 메시 오브젝트가 여럿이면(MasterChief 는 4개) 노드 월드는 한 번만 구하고 팔레트만 메시마다 만듦.
//   1) EvaluateNodeGlobals: nodes = 이번 포즈의 노드 TRS → global = 노드 월드 (nodes 와 같은 길이의 작업 공간, skin 이 쓰는 노드만 채움).
//   2) BuildSkinPalette: global + meshRest(메시 노드 기본 월드) → palette(skin.joints 길이)와 pose. 팔레트와 포즈의 의미는 Animator::UpdateSkin 의 주석 참고.
void EvaluateNodeGlobals(const std::vector<ModelNode>& nodes, const ModelSkin& skin, std::vector<DirectX::XMFLOAT4X4>& global);
void BuildSkinPalette(const ModelSkin& skin, const std::vector<DirectX::XMFLOAT4X4>& global, const DirectX::XMFLOAT4X4& meshRest,
	std::vector<DirectX::XMFLOAT4X4>& palette, GameObject::SkeletonPose& pose);
// 위 둘을 이어 부르는 편의 함수 (ApplyRestSkinPoses 처럼 메시 하나만 계산할 때).
void ComputeSkinPose(const std::vector<ModelNode>& nodes, const ModelSkin& skin, const DirectX::XMFLOAT4X4& meshRest,
	std::vector<DirectX::XMFLOAT4X4>& global, std::vector<DirectX::XMFLOAT4X4>& palette, GameObject::SkeletonPose& pose);

// 스킨 메시인데 팔레트가 비어 있는 오브젝트(편집 모드, 또는 재생 중이어도 Animator 가 아직 못 채운 경우)에 기본 자세 팔레트와
// 스켈레톤 포즈를 채움. Renderer 는 팔레트가 끝내 비어 있을 때(모델 정보를 못 찾은 경우)만 단위행렬을 씀.
// 팔레트가 이미 있으면 건드리지 않으므로 Animator 의 값이 우선함. 로드·Undo·Stop 으로 오브젝트가 새로 만들어져도 다음 호출이 다시 채움.
// 매 프레임 불러도 되도록 대상이 아닌 오브젝트는 바로 건너뜀 (로그 없음).
void ApplyRestSkinPoses(Scene& scene);

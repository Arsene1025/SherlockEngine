#include "pch.h"
#include "Scene/SkinPose.h"
#include "Scene/Scene.h"
#include "Core/AssetManager.h"
#include "Graphics/Mesh.h"
#include "Graphics/Model.h"

using namespace DirectX;

XMMATRIX ComputeNodeGlobal(const std::vector<ModelNode>& nodes, int n)
{
	XMMATRIX world = XMMatrixIdentity();
	for (size_t steps = 0; n >= 0 && static_cast<size_t>(n) < nodes.size() && steps <= nodes.size(); ++steps)
	{
		const ModelNode& node = nodes[static_cast<size_t>(n)];
		world = world * XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z) * XMMatrixRotationQuaternion(XMQuaternionNormalize(XMLoadFloat4(&node.rotation))) * XMMatrixTranslation(node.translation.x, node.translation.y, node.translation.z);
		n = node.parent;
	}
	return world;
}

const ModelSkin* FindMeshSkin(const Model& model, uint32_t meshIndex, int& meshNode)
{
	for (const ModelInstance& instance : model.instances)
	{
		if (instance.meshIndex != meshIndex || instance.skin < 0 || static_cast<size_t>(instance.skin) >= model.skins.size()) continue;
		const ModelSkin& candidate = model.skins[static_cast<size_t>(instance.skin)];
		if (candidate.joints.empty() || candidate.inverseBind.size() != candidate.joints.size()) continue;
		meshNode = instance.node;
		return &candidate;
	}
	return nullptr;
}

void EvaluateNodeGlobals(const std::vector<ModelNode>& nodes, const ModelSkin& skin, std::vector<XMFLOAT4X4>& global)
{
	// 노드 월드: 임포트 때 정해 둔 순서(부모가 항상 앞)로 한 번 훑음. 부모 번호가 범위 밖이면 루트로 봄 (임포터와 같은 규칙).
	// 자식의 로컬 * 누적된 부모의 global
	// global[n]에 저장
	for (const uint32_t n : skin.evalOrder)
	{
		const ModelNode& node = nodes[n];
		const XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&node.rotation));
		XMMATRIX world = XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z) * XMMatrixRotationQuaternion(q) * XMMatrixTranslation(node.translation.x, node.translation.y, node.translation.z);
		if (node.parent >= 0 && static_cast<size_t>(node.parent) < nodes.size())
		{
			world = world * XMLoadFloat4x4(&global[static_cast<size_t>(node.parent)]);
		}
		XMStoreFloat4x4(&global[n], world);
	}
}
// 노드 월드(위 EvaluateNodeGlobals 의 evalOrder 루프)와 아래 팔레트·포즈 식(BuildSkinPalette)은 재생 중 Animator 도 같은 함수를 부름.
// 편집 모드의 기본 자세(ApplyRestSkinPoses)도 같은 식을 써서, 재생을 시작하기 전과 후의 스킨 모양이 같은 식에서 나옴.

// 스킨 팔레트 / 스켈레톤 포즈 계산
// 행벡터 규약(DirectXMath): v' = v · M0 · M1 · ... 이라 왼쪽 행렬이 먼저 적용된다.
//
// palette[j] = 역바인드[j] · 조인트 월드[j] · (메시 노드 기본 월드)⁻¹
//   1) inverseBind[j]  : 메시 정점(바인드 포즈 메시 공간)을 조인트 j 의 로컬 공간으로 보낸다.
//                        "바인드 포즈에서 조인트 월드 행렬의 역행렬"이다.
//						  joint에 영향을 받는 정점들을 조인트 로컬로 보내서 조인트의 움직임이 정점에 반영되도록 한다.
//						  해당 과정을 거치면 정점이 조인트로부터 얼마나 떨저어졌는지 상대 좌표를 알 수 있음
//						  이 상태에서 조인트월드를 곱하면 조인트가 움직인 만큼 정점도 움직임(조인트 로컬 ㄷ좌표가 그대로 유지되니까)
//   2) jointWorld[j]   : 조인트 로컬 공간을 현재 애니메이션 포즈의 월드(씬) 공간으로 보낸다.
//                        (global = 로컬 TRS · 부모 월드, 위 EvaluateNodeGlobals 의 evalOrder 루프에서 계산됨)
//                       "바인드 포즈 대비 이 조인트가 움직인 변화량" 계산.
//                        바인드 포즈 그대로면 inverseBind · jointWorld = 단위행렬이라 정점이 제자리에 남는다.
//
//   3) inverseMesh     : 현재 엔진에서는 그릴 때 pre를 곱해서 메시를 씬 공간으로 보냄.
//						  하지만 스킨 메시는 이미 inverseBind * jointWorld를 거친 시점에 이미 씬 공간에 있음.
//						  ->jointWorld가 씬 기준이기 때문
//                        그런 상태에서 pre가 또 곱해지면 씬 공간으로 두 번 감.
//                        그레서 pre의 역행렬을 넣어서 상쇄.
//
// 정점은 자기 영향 조인트 인덱스(JOINTS_0)와 가중치(WEIGHTS_0)를 갖고 있고, 정점 셰이더가 이 배열에서
// 해당 행렬을 골라 skin = Σ 가중치 · 정점 · palette[인덱스] 로 블렌딩한다.
// SetSkinPalette 로 오브젝트에 넘기면 렌더러가 전치해서 상수 버퍼 b7 (Shaders/Mesh.hlsli 의 cbuffer Skin) 에 올려 스킨 VS 가 읽는다.
// 지난 프레임 팔레트는 같은 배치의 b13 (cbuffer PrevSkin) 으로 올라가 속도 버퍼의 이전 위치에 쓰임.
// 구동할 스킨 메시 오브젝트마다 팔레트를 만듦. 스킨이 같으면 노드 월드는 한 번만 계산함 (MasterChief 는 4개 메시가 한 스킨을 씀).
void BuildSkinPalette(const ModelSkin& skin, const std::vector<XMFLOAT4X4>& global, const XMFLOAT4X4& meshRest, std::vector<XMFLOAT4X4>& palette, GameObject::SkeletonPose& pose)
{
	const XMMATRIX inverseMesh = XMMatrixInverse(nullptr, XMLoadFloat4x4(&meshRest));
	// 팔레트 = 조인트마다 행렬 1개씩 담은 배열 (palette[j] 는 joints[j] 용). 이번 프레임 값으로 매번 새로 채운다.
	palette.assign(skin.joints.size(), XMFLOAT4X4());

	// 본 디버그 표시용 포즈. 정점이 아니라 "조인트 자체"의 위치·방향이 필요하므로 inverseBind 는 곱하지 않는다.
	pose.jointToObject.resize(palette.size());
	pose.parent = skin.jointParent;   // 조인트 인덱스 기준 부모 (루트는 음수). 부모-자식 본을 선으로 잇는 데 쓴다.
	for (size_t j = 0; j < palette.size(); ++j)
	{
		// joints[j] 는 j 번째 조인트에 대응하는 glTF 노드 인덱스. global 은 노드 인덱스로 접근한다.
		const XMMATRIX inverseBind = XMLoadFloat4x4(&skin.inverseBind[j]);
		const XMMATRIX jointWorld = XMLoadFloat4x4(&global[skin.joints[j]]);

		// 정점 셰이더가 쓰는 스키닝 행렬. (바인드 → 현재 포즈 변화량, 메시 노드 월드 상쇄)
		XMStoreFloat4x4(&palette[j], inverseBind * jointWorld * inverseMesh);

		// 조인트 로컬 → 오브젝트 공간. 오브젝트 월드를 곱하면 조인트의 실제 월드 위치가 된다.
		XMStoreFloat4x4(&pose.jointToObject[j], jointWorld * inverseMesh);
	}
}

void ComputeSkinPose(const std::vector<ModelNode>& nodes, const ModelSkin& skin, const XMFLOAT4X4& meshRest, std::vector<XMFLOAT4X4>& global, std::vector<XMFLOAT4X4>& palette, GameObject::SkeletonPose& pose)
{
	EvaluateNodeGlobals(nodes, skin, global);
	BuildSkinPalette(skin, global, meshRest, palette, pose);
}

void ApplyRestSkinPoses(Scene& scene)
{
	AssetManager* assets = scene.GetAssets();
	if (assets == nullptr) return;
	for (const auto& objectPtr : scene.GetObjects())
	{
		GameObject& object = *objectPtr;
		const Mesh* mesh = object.GetMesh();
		if (mesh == nullptr || !mesh->IsSkinned() || !object.GetSkinPalette().empty()) continue;
		const MeshSource* source = scene.GetMeshSource(mesh);
		if (source == nullptr || source->type != MeshSource::Type::Model) continue;
		const Model* model = assets->GetModel(source->modelPath);
		if (model == nullptr) continue;
		int meshNode = -1;
		const ModelSkin* skin = FindMeshSkin(*model, source->meshIndex, meshNode);
		if (skin == nullptr) continue;

		// 메시 노드의 기본 자세 월드 (조인트 월드와 같은 좌표계, options.scale 은 반영하지 않음). 메시 노드는 evalOrder 에
		// 없을 수 있어서 global 배열이 아니라 노드 표에서 직접 구함.
		XMFLOAT4X4 meshRest;
		XMStoreFloat4x4(&meshRest, meshNode >= 0 ? ComputeNodeGlobal(model->nodes, meshNode) : XMMatrixIdentity());

		std::vector<XMFLOAT4X4> global(model->nodes.size());
		std::vector<XMFLOAT4X4> palette;
		GameObject::SkeletonPose pose;
		ComputeSkinPose(model->nodes, *skin, meshRest, global, palette, pose);
		object.SetSkinPalette(std::move(palette));
		object.SetSkeletonPose(std::move(pose));
	}
}

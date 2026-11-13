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
		world = world * XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z) * XMMatrixRotationQuaternion(XMQuaternionNormalize(XMLoadFloat4(&node.rotation)))
			* XMMatrixTranslation(node.translation.x, node.translation.y, node.translation.z);
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
	for (const uint32_t n : skin.evalOrder)
	{
		const ModelNode& node = nodes[n];
		const XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&node.rotation));
		XMMATRIX world = XMMatrixScaling(node.scale.x, node.scale.y, node.scale.z) * XMMatrixRotationQuaternion(q)
			* XMMatrixTranslation(node.translation.x, node.translation.y, node.translation.z);
		if (node.parent >= 0 && static_cast<size_t>(node.parent) < nodes.size()) world = world * XMLoadFloat4x4(&global[static_cast<size_t>(node.parent)]);
		XMStoreFloat4x4(&global[n], world);
	}
}

void BuildSkinPalette(const ModelSkin& skin, const std::vector<XMFLOAT4X4>& global, const XMFLOAT4X4& meshRest,
	std::vector<XMFLOAT4X4>& palette, GameObject::SkeletonPose& pose)
{
	const XMMATRIX inverseMesh = XMMatrixInverse(nullptr, XMLoadFloat4x4(&meshRest));
	// 팔레트 = 조인트마다 행렬 1개씩 담은 배열 (palette[j] 는 joints[j] 용). 이번 프레임 값으로 매번 새로 채운다.
	palette.assign(skin.joints.size(), XMFLOAT4X4());

	// 본 디버그 표시용 포즈. 정점이 아니라 "조인트 자체"의 위치·방향이 필요하므로 inverseBind 는 곱하지 않는다.
	pose.jointToObject.resize(palette.size());
	pose.parent = skin.jointParent; // 조인트 인덱스 기준 부모 (루트는 음수). 부모-자식 본을 선으로 잇는 데 쓴다.
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

void ComputeSkinPose(const std::vector<ModelNode>& nodes, const ModelSkin& skin, const XMFLOAT4X4& meshRest,
	std::vector<XMFLOAT4X4>& global, std::vector<XMFLOAT4X4>& palette, GameObject::SkeletonPose& pose)
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

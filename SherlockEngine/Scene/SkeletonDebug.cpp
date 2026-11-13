#include "pch.h"
#include "Scene/SkeletonDebug.h"
#include "Scene/Scene.h"
#include "Scene/GameObject.h"
#include "Graphics/DebugDraw.h"
#include <vector>

using namespace DirectX;

void DrawSkeletonDebug(DebugDraw& draw, const Scene& scene, int mode)
{
	if (mode <= 0) return;

	const uint32_t boneColor = PackLineColor(0.1f, 1.0f, 0.2f);    // 본 (부모 → 자식): 초록
	const uint32_t jointColor = PackLineColor(1.0f, 0.9f, 0.1f);   // 관절 십자: 노랑

	for (const auto& objectPtr : scene.GetObjects())
	{
		const GameObject::SkeletonPose& pose = objectPtr->GetSkeletonPose();
		const size_t count = pose.jointToObject.size();
		if (count == 0) continue;
		const XMMATRIX world = objectPtr->GetWorldMatrix();

		// 조인트 월드 변환과 위치
		std::vector<XMMATRIX> joint(count);
		std::vector<XMFLOAT3> position(count);
		for (size_t j = 0; j < count; ++j)
		{
			joint[j] = XMLoadFloat4x4(&pose.jointToObject[j]) * world;
			XMStoreFloat3(&position[j], joint[j].r[3]);
		}
		// 조인트마다 "길이": 부모까지의 거리, 부모가 없으면 첫 자식까지의 거리, 둘 다 없으면 작은 기본값. 십자·축 크기가 이 값에 비례해
		// 모델 크기(단위)와 상관없이 보임.
		std::vector<float> length(count, 0.0f);
		auto distance = [&](size_t a, size_t b) { return XMVectorGetX(XMVector3Length(XMVectorSubtract(joint[a].r[3], joint[b].r[3]))); };
		for (size_t j = 0; j < count; ++j)
		{
			const int parent = j < pose.parent.size() ? pose.parent[j] : -1;
			if (parent >= 0 && static_cast<size_t>(parent) < count) length[j] = distance(j, static_cast<size_t>(parent));
		}
		for (size_t j = 0; j < count; ++j)
		{
			const int parent = j < pose.parent.size() ? pose.parent[j] : -1;
			if (parent >= 0 && static_cast<size_t>(parent) < count && length[static_cast<size_t>(parent)] == 0.0f) length[static_cast<size_t>(parent)] = length[j];   // 루트는 자식 길이를 빌림
		}

		for (size_t j = 0; j < count; ++j)
		{
			const int parent = j < pose.parent.size() ? pose.parent[j] : -1;
			if (parent >= 0 && static_cast<size_t>(parent) < count) draw.Line(position[static_cast<size_t>(parent)], position[j], boneColor);

			const float unit = length[j] > 1e-5f ? length[j] : 0.05f;
			draw.Cross(position[j], unit * 0.12f, jointColor);
			if (mode >= 2) draw.Axes(joint[j], unit * 0.35f);   // 로컬 축 (부모 스케일이 있어도 길이는 unit 기준)
		}
	}
}

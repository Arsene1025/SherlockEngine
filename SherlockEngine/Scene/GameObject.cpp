#include "pch.h"
#include "Scene/GameObject.h"
#include <algorithm>

using namespace DirectX;   // 이 파일 안에서만

bool GameObject::IsDescendantOf(const GameObject* ancestor) const
{
	for (const GameObject* p = parent; p != nullptr; p = p->parent)
		if (p == ancestor) return true;
	return false;
}

bool GameObject::SetParent(GameObject* newParent, bool keepWorld)
{
	if (newParent == this) return false;
	if (newParent != nullptr && newParent->IsDescendantOf(this)) return false;   // 순환 금지: 자손을 부모로 삼을 수 없음
	if (newParent == parent) return true;

	// keepWorld: 월드 행렬을 먼저 기억해 두고 링크를 바꾼 뒤, 새 부모 기준 로컬로 다시 분해함.
	const XMMATRIX world = GetWorldMatrix();

	if (parent != nullptr)
	{
		auto& siblings = parent->children;
		siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
	}
	parent = newParent;
	if (parent != nullptr) parent->children.push_back(this);

	if (keepWorld) SetWorldMatrix(world);
	return true;
}

XMMATRIX GameObject::GetParentWorldMatrix() const
{
	return parent != nullptr ? parent->GetWorldMatrix() : XMMatrixIdentity();
}

XMMATRIX GameObject::GetWorldMatrix() const
{
	// 행벡터 관례: 자식 로컬을 먼저 적용하고 부모로 올라감. 깊이는 보통 한두 단계라 재귀 비용은 무시할 만함.
	const XMMATRIX local = transform.GetLocalMatrix();
	return parent != nullptr ? local * parent->GetWorldMatrix() : local;
}

XMFLOAT3 GameObject::GetWorldPosition() const
{
	XMFLOAT3 p;
	XMStoreFloat3(&p, XMVector3TransformCoord(XMVectorZero(), GetWorldMatrix()));
	return p;
}

bool GameObject::SetWorldMatrix(const XMMATRIX& world)
{
	// world = local × parent.world 이므로 local = world × parent.world⁻¹. 나머지 분해(pre⁻¹, S·R·T)는 Transform 이 함.
	const XMMATRIX local = parent != nullptr ? world * XMMatrixInverse(nullptr, parent->GetWorldMatrix()) : world;
	return transform.SetLocalMatrix(local);
}

void GameObject::SetWorldPosition(const XMFLOAT3& position)
{
	// 위치만 바꾸면 되므로 행렬을 분해하지 않고 점 하나만 부모 공간으로 옮김 (pre 는 위치에 영향을 주지만 회전·스케일 값은 그대로).
	if (parent == nullptr && !transform.HasPreTransform())
	{
		transform.SetPosition(position);
		return;
	}
	XMMATRIX world = GetWorldMatrix();
	world.r[3] = XMVectorSet(position.x, position.y, position.z, 1.0f);
	SetWorldMatrix(world);
}

bool GameObject::SetWorldRotation(const XMMATRIX& rotation)
{
	// 현재 월드 행렬의 스케일·이동은 두고 회전만 갈아 끼움.
	XMVECTOR s, q, t;
	if (!XMMatrixDecompose(&s, &q, &t, GetWorldMatrix())) return false;
	const XMMATRIX world = XMMatrixScalingFromVector(s) * rotation * XMMatrixTranslationFromVector(t);
	return SetWorldMatrix(world);
}

void GameObject::DetachFromHierarchy()
{
	if (parent != nullptr)
	{
		auto& siblings = parent->children;
		siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
		parent = nullptr;
	}
	for (GameObject* child : children) child->parent = nullptr;
	children.clear();
}

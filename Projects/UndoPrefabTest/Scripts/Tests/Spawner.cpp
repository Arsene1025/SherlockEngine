#include "pch.h"
#include "Spawner.h"
#include "Core/Log.h"
#include "Scene/Scene.h"

void Spawner::Reflect(PropertyVisitor& v)
{
	v.String("prefab", prefab);
	v.Float("interval", interval, 0.05f);
	v.Int("maxSpawns", maxSpawns);
	v.Float("spacing", spacing, 0.1f);
	v.Int("logEvery", logEvery);
}

void Spawner::Start()
{
	m_timer = 0.0f;
	m_spawned = 0;
	m_frame = 0;
	m_ids.clear();
}

void Spawner::Update(float dt)
{
	++m_frame;
	m_timer += dt;
	while (m_timer >= interval && m_spawned < maxSpawns)
	{
		m_timer -= interval;
		const DirectX::XMFLOAT3 origin = GetOwner().GetWorldPosition();
		const DirectX::XMFLOAT3 position(origin.x + spacing * static_cast<float>(m_spawned), origin.y, origin.z);
		if (GameObject* created = Instantiate(prefab, position)) m_ids.push_back(created->GetId());   // 포인터가 아니라 ID 를 기억함
		++m_spawned;
	}

	if (logEvery > 0 && m_frame % logEvery == 0)
	{
		int alive = 0;
		for (uint64_t id : m_ids) if (FindById(id) != nullptr) ++alive;
		Log::Info("Spawner: 만든 수 %d, ID 로 찾아지는 수 %d, 씬 오브젝트 %zu, 메시 %zu, 재질 %zu (프레임 %d)", static_cast<int>(m_ids.size()), alive,
			GetScene().GetObjects().size(), GetScene().GetMeshes().size(), GetScene().GetMaterials().size(), m_frame);
	}
}

SHERLOCK_SCRIPT(Spawner)

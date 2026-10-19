#pragma once
#include <cstdint>
#include <vector>
#include "Scene/Script.h"

// 실험용 스크립트 (UndoPrefabTest 프로젝트, Scripts\Tests\): Instantiate + 지연 Destroy + 안정 ID 검증.
// interval 초마다 프리팹을 자기 위치(+ i * spacing 만큼 옆)에 만들고 만든 오브젝트의 ID 를 기억함. logEvery 프레임마다
// "만든 수 / ID 로 다시 찾아지는 수 / 씬 오브젝트 수" 를 로그로 남김 — Lifetime 이 붙은 프리팹이면 살아 있는 수가 상한에 머물러야 하고,
// 지워진 오브젝트의 ID 는 FindById 에서 nullptr 가 되어야 함 (포인터를 들고 있었다면 dangling).
class Spawner : public Script
{
public:
	const char* GetTypeName() const override;   // SHERLOCK_SCRIPT 가 정의함 (.cpp)
	void Reflect(PropertyVisitor& v) override;
	void Start() override;
	void Update(float dt) override;

	std::string prefab = "Prefabs\\Bullet.prefab";   // Assets 기준 상대 경로
	float interval = 0.2f;
	int maxSpawns = 30;
	float spacing = 0.6f;
	int logEvery = 30;

private:
	float m_timer = 0.0f;
	int m_spawned = 0;
	int m_frame = 0;
	std::vector<uint64_t> m_ids;
};

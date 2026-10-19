#pragma once
#include "Scene/Script.h"

// 실험용 스크립트 (UndoPrefabTest 프로젝트, Scripts\Tests\): 지연 Destroy 검증. 재생 시작 뒤 seconds 초가 지나면 자기 자신을 Destroy 함.
// Update 안에서 DestroySelf 를 불러도 순회가 깨지지 않는지(프레임 끝에 지워짐), 지운 뒤 FindById 가 nullptr 를 돌려주는지 확인하는 데 씀.
// 프리팹(총알)에 붙여 Spawner 가 만든 인스턴스가 스스로 사라지게 한다.
class Lifetime : public Script
{
public:
	const char* GetTypeName() const override;   // SHERLOCK_SCRIPT 가 정의함 (.cpp)
	void Reflect(PropertyVisitor& v) override;
	void Start() override;
	void Update(float dt) override;

	float seconds = 1.0f;

private:
	float m_elapsed = 0.0f;
};

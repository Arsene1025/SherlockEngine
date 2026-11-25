#pragma once
#include "Scene/Script.h"

// World Space 캔버스 이름표가 카메라를 보게 (8단계 테스트). y 축으로만 돔 — 캔버스 앞면(+z 쪽에서 보는 면)이 카메라 쪽
class Billboard : public Script
{
public:
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& v) override;
	void Update(float dt) override;

	bool yawOnly = true;
};

#include "pch.h"
#include "Lifetime.h"

void Lifetime::Reflect(PropertyVisitor& v)
{
	v.Float("seconds", seconds, 0.1f);
}

void Lifetime::Start()
{
	m_elapsed = 0.0f;
}

void Lifetime::Update(float dt)
{
	m_elapsed += dt;
	if (m_elapsed >= seconds) DestroySelf();   // 프레임 끝에 지워짐. 이 Update 의 나머지는 그대로 실행됨
}

SHERLOCK_SCRIPT(Lifetime)

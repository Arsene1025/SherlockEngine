#include "pch.h"
#include "Test.h"

// 쓸 수 있는 것: Translate / TranslateLocal / Rotate / LookAt / SetPosition / SetScale, GetInput().IsKeyDown(VK_UP),
//              GetTime(), Find("이름")->GetBehaviour<다른스크립트>(), GetComponent<Rigidbody>() (#include "Game/Components/Rigidbody.h").
// 고친 뒤에는 빌드(Ctrl+Shift+B)하고 다시 실행해야 함.

void Test::Reflect(PropertyVisitor& v)
{
	v.Float("speed", speed);
}

void Test::Start()
{
}

void Test::Update(float dt)
{
	// 예: Rotate(0.0f, speed * dt, 0.0f);
	// 예: if (GetInput().IsKeyDown(VK_UP)) Translate(0.0f, 0.0f, speed * dt);
	(void)dt;
}

SHERLOCK_SCRIPT(Test)

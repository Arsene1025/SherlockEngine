#include "pch.h"
#include "Scene/Dissolve.h"

void Dissolve::Reflect(PropertyVisitor& v)
{
	// 이름이 곧 씬 파일의 키. 바꾸면 기존 씬의 값이 기본값으로 돌아감.
	v.Float("amount", amount, 0.01f);
	v.Float("edgeWidth", edgeWidth, 0.005f);
	v.Color("edgeColor", edgeColor);
	v.Float("edgeIntensity", edgeIntensity, 0.05f);
}

SHERLOCK_BEHAVIOUR(Dissolve)

#include "pch.h"
#include "Scene/Highlight.h"

void Highlight::Reflect(PropertyVisitor& v)
{
	// 이름이 곧 씬 파일의 키. 바꾸면 기존 씬의 값이 기본값으로 돌아감.
	v.Color("tintColor", tintColor);
	v.Float("tintAmount", tintAmount, 0.01f);
	v.Color("flashColor", flashColor);
	v.Float("flashIntensity", flashIntensity, 0.05f);
	v.Color("rimColor", rimColor);
	v.Float("rimIntensity", rimIntensity, 0.05f);
	v.Float("rimPower", rimPower, 0.05f);
}

SHERLOCK_BEHAVIOUR(Highlight)

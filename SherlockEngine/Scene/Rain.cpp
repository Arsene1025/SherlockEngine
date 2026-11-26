#include "pch.h"
#include "Scene/Rain.h"

void Rain::Reflect(PropertyVisitor& v)
{
	// 이름이 곧 씬 파일의 키. 바꾸면 기존 씬의 값이 기본값으로 돌아감.
	v.Float("intensity", intensity, 0.01f);
	v.Int("maxDrops", maxDrops);
	v.Float("areaRadius", areaRadius, 0.1f);
	v.Float("areaHeight", areaHeight, 0.1f);
	v.Float("fallSpeed", fallSpeed, 0.05f);
	v.Float3("wind", wind, 0.05f);
	v.Float("streakLength", streakLength, 0.005f);
	v.Float("streakWidth", streakWidth, 0.0005f);
	v.Float3("color", color, 0.01f);
	v.Float("brightness", brightness, 0.01f);
	v.Float("nearFade", nearFade, 0.01f);
	v.Float("wetness", wetness, 0.01f);
	v.Float("wetTime", wetTime, 0.1f);
	v.Float("dryTime", dryTime, 0.1f);
	v.Float("occlusionSize", occlusionSize, 0.5f);
	v.Int("occlusionResolution", occlusionResolution);
	v.Int("seed", seed);
	v.Bool("splashes", splashes);
	v.Int("maxSplashes", maxSplashes);
	v.Bool("ripples", ripples);
	v.Float("puddles", puddles, 0.01f);
}

SHERLOCK_BEHAVIOUR(Rain)

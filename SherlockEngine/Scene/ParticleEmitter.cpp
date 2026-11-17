#include "pch.h"
#include "Scene/ParticleEmitter.h"

void ParticleEmitter::Reflect(PropertyVisitor& v)
{
	// 이름이 곧 씬 파일의 키. 바꾸면 기존 씬의 값이 기본값으로 돌아감.
	v.Int("maxParticles", maxParticles);
	v.Float("spawnRate", spawnRate, 1.0f);
	v.Float("lifetime", lifetime, 0.05f);
	v.Float("startSpeed", startSpeed, 0.05f);
	v.Float("speedVariation", speedVariation, 0.01f);
	v.Float("spreadAngle", spreadAngle, 0.5f);
	v.Float("emitRadius", emitRadius, 0.01f);
	v.Float3("gravity", gravity, 0.05f);
	v.Float("startSize", startSize, 0.005f);
	v.Float("endSize", endSize, 0.005f);
	v.Float3("color", color, 0.01f);
	v.Float("intensity", intensity, 0.05f);
	v.Float3("endTint", endTint, 0.01f);
	v.Int("blendMode", blendMode);
	v.Float("opacity", opacity, 0.01f);
	v.Float("softness", softness, 0.01f);
	v.Bool("collide", collide);
	v.Float("bounce", bounce, 0.01f);
}

SHERLOCK_BEHAVIOUR(ParticleEmitter)

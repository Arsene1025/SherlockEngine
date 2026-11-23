#include "pch.h"
#include "Scene/Ocean.h"
#include <algorithm>
#include <cmath>

namespace
{
	constexpr float kGravity = 9.81f;
	constexpr float kPi = 3.14159265358979f;

	// 시드로 만든 작은 LCG (std::rand 는 전역 상태라 쓰지 않음). [0, 1). check_gerstner_normals.py 의 lcg 와 같은 식.
	struct Lcg
	{
		uint32_t state;
		explicit Lcg(int seed)
			: state(static_cast<uint32_t>(seed) * 747796405u + 2891336453u)
		{}
		float Next()
		{
			state = state * 1664525u + 1013904223u;
			return static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
		}
	};
}

void Ocean::Reflect(PropertyVisitor& v)
{
	// 이름이 곧 씬 파일의 키. 바꾸면 기존 씬의 값이 기본값으로 돌아감.
	v.Int("waveCount", waveCount);
	v.Float("windDirection", windDirection, 0.5f);
	v.Float("directionSpread", directionSpread, 0.5f);
	v.Float("wavelengthMin", wavelengthMin, 0.05f);
	v.Float("wavelengthMax", wavelengthMax, 0.1f);
	v.Float("amplitude", amplitude, 0.005f);
	v.Float("steepness", steepness, 0.01f);
	v.Float("speed", speed, 0.01f);
	v.Int("seed", seed);
	v.Float("fadeStart", fadeStart, 1.0f);
	v.Float("fadeEnd", fadeEnd, 1.0f);
}

int Ocean::BuildWaves(OceanWave (&out)[kMaxWaves]) const
{
	const int count = std::clamp(waveCount, 1, kMaxWaves);
	const float lengthMin = (std::max)(wavelengthMin, 0.1f);
	const float lengthMax = (std::max)(wavelengthMax, lengthMin);
	const float steepnessEach = std::clamp(steepness, 0.0f, 1.0f) / static_cast<float>(count);
	Lcg random(seed);
	for (int i = 0; i < count; ++i)
	{
		// 파장: 가장 긴 것에서 가장 짧은 것까지 등비. 진폭은 파장에 비례 (모든 파도의 가파름 kA 가 같음).
		const float t = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
		const float length = lengthMax * std::pow(lengthMin / lengthMax, t);
		const float angle = (windDirection + directionSpread * (2.0f * random.Next() - 1.0f)) * (kPi / 180.0f);
		OceanWave& wave = out[i];
		wave.dirX = std::cos(angle);
		wave.dirZ = std::sin(angle);
		wave.k = 2.0f * kPi / length;
		wave.amplitude = amplitude * length / lengthMax;
		wave.steepness = steepnessEach;
		wave.omega = std::sqrt(kGravity * wave.k) * speed;   // 깊은 물의 분산 관계. 긴 파도가 더 빠름
		wave.phase0 = 2.0f * kPi * random.Next();
	}
	return count;
}

float Ocean::MaxDisplacement(const OceanWave* waves, int count)
{
	float vertical = 0.0f;
	float horizontal = 0.0f;
	for (int i = 0; i < count; ++i)
	{
		vertical += std::fabs(waves[i].amplitude);
		horizontal += waves[i].steepness / waves[i].k;
	}
	return vertical + horizontal;
}

SHERLOCK_BEHAVIOUR(Ocean)

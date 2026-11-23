#pragma once
#include "Core/EngineApi.h"   // SHERLOCK_API (엔진 DLL 내보내기)
#include "Scene/Behaviour.h"
#include <cstdint>

// Gerstner 파도 하나 (Ocean::BuildWaves 의 결과). 위상은 φ(t) = omega · t + phase0.
struct OceanWave
{
	float dirX = 1.0f;   // 진행 방향 (월드 xz 단위 벡터)
	float dirZ = 0.0f;
	float k = 1.0f;           // 파수 2π / 파장 (1/m)
	float amplitude = 0.0f;   // 진폭 (m)
	float steepness = 0.0f;   // 수평으로 마루 쪽에 모이는 정도 (Σ ≤ 1)
	float omega = 0.0f;       // 각속도 (rad/s) = sqrt(g · k) · speed
	float phase0 = 0.0f;      // 초기 위상 (rad)
};

// 잔잔한 배경 바다 (docs/plan/ocean-gerstner-plan.html). 평면 메시를 가진 오브젝트에 붙이면 렌더러가 그 드로우만 바다 정점
// 셰이더로 바꿔 Gerstner 파도를 더함. 이 컴포넌트는 값만 들고 있음 (Update 를 쓰지 않아 편집 모드에서도 파도가 움직임).
//
// 파도는 하나씩 넣지 않고 바람 방향·퍼짐·파장 범위·진폭·경사·시드로 최대 kMaxWaves 개를 만듦 (BuildWaves). 계산은 GPU 정점
// 셰이더에서만 하고 CPU 가 파도 높이를 읽지 않음 (부력 없음). 그림자는 받기만 하고 만들지 않음.
// 씬에 하나: 첫 번째 바다만 파도를 받고, 둘째부터는 일반 평면으로 그림 (로그 경고 한 번). 바다 오브젝트의 피치·롤은 지원하지 않음.
// 색·거칠기는 오브젝트의 재질로 정함 (짙은 청록 알베도, 금속성 0, 거칠기 0.06 안팎, ior 1.33).
class SHERLOCK_API Ocean : public Behaviour
{
public:
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& visitor) override;

	static constexpr int kMaxWaves = 8;   // ShaderConstants.h 의 kMaxOceanWaves 와 같아야 함

	// 파도를 만듦. 같은 값이면 같은 파도 (난수는 seed 로 만든 LCG). 돌려주는 값 = 파도 수 [1, kMaxWaves].
	int BuildWaves(OceanWave (&out)[kMaxWaves]) const;
	// 감쇠 전 최대 변위 (m): 수직 Σ A + 수평 Σ s / k 를 더한 값. 컬링 경계 구에 더함.
	static float MaxDisplacement(const OceanWave* waves, int count);

	int waveCount = 6;               // 파도 수. [1, 8] 로 자름
	float windDirection = 30.0f;     // 주 방향 (도, +X 에서 +Z 쪽으로)
	float directionSpread = 40.0f;   // 파도 방향이 주 방향에서 퍼지는 반각 (도)
	float wavelengthMin = 4.0f;      // 파장 범위 (m). 그 사이를 등비로 나눔
	float wavelengthMax = 32.0f;
	float amplitude = 0.25f;   // 가장 긴 파도의 진폭 (m). 파도 i 는 amplitude × Lᵢ / Lmax → 모든 파도의 kA 가 같음 (기본 ≈ 0.05)
	float steepness = 0.4f;    // Σ s. [0, 1] 로 자르고 파도마다 같은 몫으로 나눔. 1 에 가까우면 마루가 뾰족함
	float speed = 1.0f;        // ω 배율. 0 이면 파도가 멈춤 (검증용)
	int seed = 1;              // 방향 퍼짐·초기 위상 난수 시드
	// 거리 감쇠 (m): 바다 오브젝트 원점에서 이 거리 사이에서 진폭·경사를 1 → 0 으로 (smoothstep). 가장자리의 성긴 격자가 짧은 파도를
	// 담지 못해 생기는 계단·물결무늬를 막고, 먼 바다는 평평한 거울처럼 하늘을 비춤. fadeEnd ≤ fadeStart 면 감쇠 없음.
	float fadeStart = 80.0f;
	float fadeEnd = 300.0f;
};

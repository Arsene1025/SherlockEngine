#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "Graphics/ShaderConstants.h"
#include "Scene/GameObject.h"
#include "Scene/Ocean.h"
#include "Core/Log.h"
#include <cmath>

// Gerstner 바다 (Renderer 의 멤버 함수를 파일만 나눠 구현함). 설계: docs/plan/ocean-gerstner-plan.html.
//
// 프레임 안에서 바다가 지나는 길:
//   BuildDrawList  오브젝트에 Ocean 컴포넌트 → CollectOcean (첫 번째 하나) → 그 드로우만 Pass_Ocean·Pass_GBufferOcean PSO,
//                  그림자 PSO 없음, 경계 구에 최대 변위를 더함
//   Prepare        UploadOceanConstants (b9: 파도, 이번·지난 위상, 감쇠, 원점). 메인·미리보기 뷰가 같은 값을 씀
//   G-buffer 패스  OceanVS + OceanGBufferPS (GBufferPS + 픽셀 단위 노멀) → 디퍼드 조명이 IBL 반사·그림자를 그대로 적용
//   포워드 패스    OceanVS + OceanPS (BasicPS + 픽셀 단위 노멀, 디퍼드를 끈 경우)

using namespace DirectX;

static_assert(Ocean::kMaxWaves == static_cast<int>(kMaxOceanWaves), "Ocean::kMaxWaves 와 kMaxOceanWaves (b9 배열 크기) 가 같아야 함");

bool Renderer::CollectOcean(const GameObject& object, const XMMATRIX& world)
{
	const Ocean* ocean = nullptr;
	for (const auto& behaviour : object.GetBehaviours())
	{
		const Ocean* candidate = dynamic_cast<const Ocean*>(behaviour.get());
		if (candidate != nullptr && candidate->enabled)
		{
			ocean = candidate;
			break;
		}
	}
	if (ocean == nullptr) return false;
	if (m_frameOcean.active)
	{
		// 씬에 하나만: b9 를 프레임에 한 번만 올리므로 둘째부터는 일반 평면으로 그림.
		if (!m_oceanExtraWarned)
		{
			m_oceanExtraWarned = true;
			Log::Warn("Ocean: 바다가 둘 이상임. 첫 번째 바다만 파도를 받고 '%s' 는 일반 평면으로 그림.", object.GetName().c_str());
		}
		return false;
	}
	m_frameOcean.active = true;
	m_frameOcean.objectId = object.GetId();
	m_frameOcean.waveCount = ocean->BuildWaves(m_frameOcean.waves);
	XMStoreFloat3(&m_frameOcean.origin, world.r[3]);
	m_frameOcean.fadeStart = ocean->fadeStart;
	m_frameOcean.fadeEnd = ocean->fadeEnd;
	return true;
}

void Renderer::UploadOceanConstants(float totalTime)
{
	// 바다가 없어도 올림 (파도 0 개). b9 는 오브젝트 셋에 항상 묶여 있고, D3D12 의 Dynamic 상수버퍼는 프레임마다 한 번은 써야
	// 바인딩됨 (b7·b13 과 같은 규칙). 그래서 이 함수에는 일찍 끝나는 경로가 없음.
	OceanConstants constants = {};
	if (m_frameOcean.active)
	{
		// 시간: --ocean-time 이면 고정. 지난 위상은 지난 프레임에 실제로 쓴 시간으로 만듦 (time − dt 가 아님). 처음 프레임·리셋 뒤·
		// 바다가 바뀐 프레임·시간 고정이면 지난 = 이번 (속도 0). 일시정지에서는 totalTime 이 멈추므로 저절로 0.
		// totalTime 은 float 라 실행 시간이 길면 정밀도가 떨어짐 (하루 ≈ 8 ms 단위). 위상 감기는 double 로 해서 ω·t 를 셰이더에서
		// 곱할 때보다 낫지만, 입력 자체의 해상도는 그대로임.
		const bool fixedTime = m_settings.oceanTimeOverride >= 0.0f;
		const double time = fixedTime ? static_cast<double>(m_settings.oceanTimeOverride) : static_cast<double>(totalTime);
		const bool samePrev = m_oceanHasPrev && m_oceanPrevObject == m_frameOcean.objectId && !fixedTime;
		const double prevTime = samePrev ? m_oceanPrevTime : time;
		constexpr double kTwoPi = 6.283185307179586;
		for (int i = 0; i < m_frameOcean.waveCount; ++i)
		{
			const OceanWave& wave = m_frameOcean.waves[i];
			const double phaseNow = std::fmod(static_cast<double>(wave.omega) * time + wave.phase0, kTwoPi);
			const double phasePrev = std::fmod(static_cast<double>(wave.omega) * prevTime + wave.phase0, kTwoPi);
			constants.waveA[i] = XMFLOAT4(wave.dirX, wave.dirZ, wave.k, wave.amplitude);
			constants.waveB[i] = XMFLOAT4(wave.steepness, static_cast<float>(phaseNow), static_cast<float>(phasePrev), 0.0f);
		}
		constants.params = XMFLOAT4(static_cast<float>(m_frameOcean.waveCount), m_frameOcean.fadeStart, m_frameOcean.fadeEnd, 0.0f);   // fadeEnd ≤ fadeStart 면 셰이더가 감쇠하지 않음
		constants.origin = XMFLOAT4(m_frameOcean.origin.x, 0.0f, m_frameOcean.origin.z, 0.0f);
		m_oceanPrevTime = time;
		m_oceanPrevObject = m_frameOcean.objectId;
		m_oceanHasPrev = true;
	}
	else
	{
		m_oceanHasPrev = false;
	}
	m_device->UpdateBuffer(m_oceanCB, &constants, sizeof(constants));
}

#pragma once
#include "Core/EngineApi.h"   // SHERLOCK_API (엔진 DLL 내보내기)
#include "Scene/Behaviour.h"
#include <DirectXMath.h>

// 비 (docs/plan/rain-plan.html). 어느 오브젝트에나 붙이면 카메라 주변에 빗줄기가 내리고, 비를 맞는 표면이 젖음.
// 오브젝트의 위치는 쓰지 않음 — 비는 늘 카메라를 따라감 (빗방울은 월드 격자에 고정되어 카메라가 움직여도 미끄러지지 않음).
//
// 빗줄기와 젖음은 위에서 내려다본 깊이 맵 (차폐 맵) 하나를 같이 써서, 지붕·처마·회랑 아래에는 비가 내리지 않고 바닥도 마름.
// 이 컴포넌트는 값만 들고 있음 (Update 를 쓰지 않아 편집 모드에서도 비가 내림). 게임 스크립트가 GetBehaviour<Rain>() 으로
// intensity 를 바꾸면 빗줄기는 바로, 젖음은 wetTime·dryTime 에 따라 천천히 따라감.
// 씬에 하나: 첫 번째 Rain 만 쓰고 둘째부터는 무시함 (로그 경고 한 번).
class SHERLOCK_API Rain : public Behaviour
{
public:
	const char* GetTypeName() const override;
	void Reflect(PropertyVisitor& visitor) override;

	float intensity = 1.0f;   // 비 세기 0..1. 그리는 빗방울 수 = maxDrops × intensity, 젖음 목표 = wetness × intensity
	int maxDrops = 20000;     // 세기 1 일 때 빗방울 수. [0, 100000] 으로 자름. 20000 보다 적으면 물결·튐도 그 비율만큼 줄어듦
	float areaRadius = 15.0f;   // 카메라 중심 상자의 가로 반폭 (m)
	float areaHeight = 12.0f;   // 상자 높이 (m). 카메라 아래 1/3, 위 2/3
	float fallSpeed = 9.0f;     // 낙하 속력 (m/s)
	DirectX::XMFLOAT3 wind = DirectX::XMFLOAT3(1.5f, 0.0f, 0.5f);   // 바람 (m/s, 월드 xz 만 씀). 빗줄기가 기우는 정도
	float streakLength = 0.4f;    // 빗줄기 길이 (m)
	float streakWidth = 0.004f;   // 빗줄기 폭 (m). 화면에서는 최소 1 픽셀로 키우고 키운 만큼 어둡게 함
	DirectX::XMFLOAT3 color = DirectX::XMFLOAT3(0.7f, 0.75f, 0.8f);   // 빗줄기 색 (선형)
	float brightness = 1.0f;      // 빗줄기 밝기 (HDR 가산, 환경광 세기를 곱함. 1 픽셀보다 가는 줄은 폭 비율만큼 더 어두움)
	float nearFade = 0.5f;        // 카메라에서 이 거리(m) 안의 빗줄기는 흐려짐 (화면을 덮는 큰 줄 방지)
	float wetness = 1.0f;         // 세기 1 일 때 젖음 목표 (0 이면 빗줄기만)
	float wetTime = 20.0f;        // 젖음이 목표 쪽으로 63% 가는 시간 (초)
	float dryTime = 60.0f;        // 마를 때의 같은 시간 (초)
	float occlusionSize = 80.0f;  // 차폐 맵 한 변 (m). 카메라 xz 중심
	int occlusionResolution = 1024;   // 차폐 맵 해상도 (512·1024·2048 중 가까운 것)
	int seed = 1;                 // 빗방울 해시 씨앗
	// ---- 3단계 ----
	bool splashes = true;   // 지붕·바닥에서 튀는 물방울. 물결과 같은 자리·순간에 터지므로 수는 물결과 같이 maxDrops·세기를 따름
	bool ripples = true;    // 위를 보는 젖은 면의 동심원 물결. 링이 생기는 칸 수 = maxDrops / 20000 (최대 1) × 세기
	float puddles = 0.5f;   // 웅덩이 양 0..1 (0 = 없음). 젖음이 충분할 때부터 자람
};

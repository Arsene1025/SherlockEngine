#pragma once

// 그림자 설정. 씬의 환경(SceneEnvironment::shadow)에 들어가 씬 JSON 으로 저장·복원·되돌리기됨.
// 예전에는 RenderSettings(렌더러의 런타임 설정)에 있어서 씬을 열 때마다 직전 슬라이더 값이 남았고, 씬 크기에
// 맞춘 값이 파일에 없었음. 그림자를 켜고 끄는 스위치(RenderSettings::shadows, F8)는 씬이 아니라 렌더러 쪽에
// 그대로 둠.
struct ShadowSettings
{
	float bias = 0.0015f;         // 수신자 깊이에서 빼는 값 (광원 클립 z 단위, 0..1)
	float strength = 1.0f;        // 0 = 그림자 없음, 1 = 완전히 어둡게
	float normalOffset = 0.3f;    // 노말맵이 그림자 UV 를 미는 배율. 0 = 노말맵 영향 없음. 월드 거리로 약 normalOffset × |노멀 차이| m (Ortho size 와 무관)
	float orthoSize = 60.0f;      // 광원 직교 투영의 한 변 (월드 단위). 씬 전체를 덮어야 함. 맵 밖은 빛을 받는 것으로 간주됨
	float distance = 40.0f;       // 광원 눈의 위치 = 씬 중심 − 방향 × 거리
	float localNormalBias = 1.0f; // 원근 뷰(스팟·포인트)의 노멀 오프셋. 단위는 그 위치의 섀도 텍셀 크기

	// 방향광 CSM (캐스케이드 섀도 맵). cascadeEnabled 가 꺼져 있으면 직교 뷰 하나 (orthoSize·distance, CSM
	// 아틀라스 전체 2048²), 켜져 있으면 카메라 절두체를 cascadeCount(2~4)개로 나눠 가까운 쪽에 해상도를
	// 몰아줌. 직교 카메라에서는 켜져 있어도 단일 뷰. CSM 일 때 distance 는 "절두체 조각 뒤쪽 캐스터까지 잡는
	// 거리"로, orthoSize 는 바이어스 환산 기준으로만 쓰임.
	bool cascadeEnabled = true;
	int cascadeCount = 4;
	float cascadeDistance = 200.0f;  // 그림자를 그리는 최대 카메라 거리 (카메라 far 와 작은 쪽)
	float cascadeLambda = 0.7f;      // 분할식의 로그(1)·균등(0) 섞는 비율
	float cascadeBlend = 0.15f;      // 캐스케이드 경계에서 다음 캐스케이드와 섞는 구간 (그 캐스케이드 깊이 길이의 비율, 0 = 끔). 마지막 캐스케이드는 같은 폭으로 그림자가 서서히 사라짐
};

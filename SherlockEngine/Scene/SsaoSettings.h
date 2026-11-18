#pragma once

// SSAO (화면 공간 앰비언트 오클루전) 설정. 씬의 환경(SceneEnvironment::ssao)에 들어가 씬 JSON 의 environment.ssao 로
// 저장·복원·되돌리기됨 — 반경은 씬의 크기(미터)에 맞춰야 하므로 씬 값임. Renderer 는 디퍼드 프레임에서만 씀 (G-buffer 의
// 노멀·깊이를 읽기 때문). 원리와 패스 구조: docs/plan/deferred-clustered-ssao-record.html.
struct SsaoSettings
{
	bool enabled = false;
	float radius = 0.5f;       // 반구 반경 (월드 m). 이보다 먼 물체는 가림으로 세지 않음 (범위 검사)
	float bias = 0.025f;       // 샘플 깊이와 장면 깊이를 비교할 때의 여유 (m). 평평한 면이 자기 자신을 가리는 얼룩을 막음
	float intensity = 1.0f;    // 가림 비율에 곱함 (1 = 그대로)
	float power = 1.5f;        // 최종 AO 에 거듭제곱 (대비를 높임)
	int sampleCount = 16;      // 반구 샘플 수 (1..32)
	bool multiply = false;     // 재질 AO 와 합치는 방식: false = min (같은 가림을 두 번 세지 않음), true = 곱
};

#pragma once

// 후처리 설정. 씬의 환경(SceneEnvironment::post)에 들어가 씬 JSON 으로 저장·복원·되돌리기됨.
// Renderer 는 이 값을 읽어 HDR 구간(블러·블룸)과 톤 패스(GreyScale 등 색 효과)를 정함.
// 효과를 추가할 때: 여기에 필드 → SceneSerializer 의 환경 JSON → DebugUI 의 후처리 섹션 → Renderer 의 후처리
// 체인 순으로 늘림.
//
// 순서는 Renderer 가 고정함 (사용자가 바꿀 수 없음):
//   HDR 구간: TAA → 모션 블러 → 블러 → 블룸   (선형 HDR 값에서 해야 1 을 넘는 밝기가 살아 있음. 모션 블러가 블룸 앞이라
//             밝은 줄무늬가 번진 뒤에 퍼짐)
//   톤 패스 : 노출 → ACES → 색 효과(GreyScale …)
struct PostProcessSettings
{
	// 가우시안 블러 (가로·세로 분리). 반경은 "720p 기준 픽셀"이라 출력 높이에 비례해 늘고 줆 — 씬 뷰 크기를
	// 바꾸거나 카메라 미리보기(작은 타깃)에서도 모양이 같음.
	bool blurEnabled = false;
	float blurRadius = 4.0f;

	// 블룸: 밝기가 threshold 를 넘는 부분을 뽑아(knee 로 부드럽게) 축소 체인으로 흐린 뒤 intensity 만큼 더함.
	bool bloomEnabled = false;
	float bloomThreshold = 1.0f;
	float bloomKnee = 0.5f;
	float bloomIntensity = 0.5f;

	// 톤 패스의 색 효과. 0 = 원본, 1 = 완전한 흑백.
	float greyscale = 0.0f;

	// 모션 블러 (속도 버퍼를 따라 번짐, Shaders/MotionBlurPixelShader.hlsl). 카메라·오브젝트 움직임 모두.
	//   intensity  셔터 배율: 한 프레임 이동량 × 이 값이 번지는 길이 (0.5 = 180° 셔터). 0~1
	//   maxRadius  번지는 길이 상한, 720p 기준 픽셀 (blurRadius 와 같은 관례 — 출력 높이에 비례). 순간이동을 막음
	//   samples    샘플 수 4~32
	//   targetFps  기준 프레임레이트. 번지는 길이를 "이 fps 였다면 한 프레임에 움직였을 거리" 로 맞춤 — 실제 프레임이
	//              300·1600 fps 로 돌아도 60 fps 와 같은 길이로 번짐 (속도 × (1/targetFps) / dt). 0 = 맞추지 않음 (한 프레임 이동량 그대로)
	bool motionBlurEnabled = false;
	float motionBlurIntensity = 0.5f;
	float motionBlurMaxRadius = 32.0f;
	int motionBlurSamples = 12;
	float motionBlurTargetFps = 60.0f;

	// TAA (Shaders/TaaPixelShader.hlsl): 투영을 프레임마다 픽셀 아래로 조금씩 흔들고(지터) 지난 결과와 섞어 계단·깜빡임을 줄임.
	//   feedback  이번 프레임의 비중 (0.1 = 약 10 프레임 평균). 1 이면 섞지 않음 (배관 검증용). 0.02~1
	bool taaEnabled = false;
	float taaFeedback = 0.1f;

	bool AnyHdrEffect() const { return blurEnabled || bloomEnabled || motionBlurEnabled || taaEnabled; }
};

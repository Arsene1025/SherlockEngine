#pragma once
#include "Scene/Script.h"

// 실험용 스크립트 (QuaternionLookTest 프로젝트, Scripts\Tests\): 쿼터니언 누적 실험 — "마우스로 원을 그리면 지평선이 기운다" 를 재현함 (phase-2 3.5, phase-11f 3.4 의 근거).
// Sample 프로젝트의 예제(Rotator·PlayerController)와 달리 게임에 쓰라고 만든 것이 아니라 문서의 주장을 확인하려고 만든 것이라, 실험 전용 프로젝트의 Tests 폴더에 둔다.
// 프로젝트 솔루션이 Scripts\**\*.cpp 를 재귀 와일드카드로 컴파일하므로 하위 폴더에 있어도 QuaternionLookTestEditor.exe 에 포함된다 (다른 프로젝트의 에디터에는 없음).
//
// 마우스 대신 가상 마우스가 (yaw, pitch) 평면에서 반지름 radiusDeg 인 원을 framesPerLoop 프레임에 한 바퀴 그림. 한 바퀴의 증분 합은 정확히 0 이므로
// "올바른" 누적이라면 카메라는 매 바퀴 출발 자세로 돌아와야 함. 증분을 쿼터니언에 어떻게 곱하느냐가 mode:
//   mode 0  q ← (q_pitch · q_yaw) · q   두 증분을 모두 자기(로컬) 축으로 곱함. 6자유도 비행 카메라식. 원을 한 바퀴 돌 때마다
//           roll 이 원이 감싼 입체각(2π(1−cos r))만큼 남음 — 부동소수점 오차가 아니라 회전 합성의 비가환성(홀로노미)에서 오는 것이라 크고 결정적임.
//   mode 1  q ← (q_pitch · q) · q_yaw   pitch 는 로컬 X, yaw 는 월드 Y. Rx(p)·Ry(y) 꼴이 유지되어 roll 이 생기지 않음 (FPS 카메라 관례, Camera::Rotate 와 같은 결과).
// 재생 중 자기 오브젝트의 CameraComponent priority 를 100 으로 올려 활성 카메라가 되게 하고, 누적한 쿼터니언을 매 프레임 월드 회전으로 써 넣음.
// 자동 검증 예 (Binaries\Debug\QuaternionLookTestEditor.exe):  --scene=quat_roll_local.json --play=1 --exit-after=372 --screenshot=...   (3 바퀴 뒤 스크린샷)
class QuaternionLook : public Script
{
public:
	const char* GetTypeName() const override;   // SHERLOCK_SCRIPT 가 정의함 (.cpp)
	void Reflect(PropertyVisitor& v) override;
	void Start() override;
	void Update(float dt) override;

	int mode = 0;                 // 0: 로컬 yaw·pitch 누적 (roll 누적), 1: 월드 yaw + 로컬 pitch (roll 0)
	float radiusDeg = 17.0f;      // 가상 마우스 원의 반지름 (도). 한 바퀴당 roll ≈ 2π(1−cos r) rad → 17° 면 약 15.7°
	int framesPerLoop = 120;      // 한 바퀴에 쓰는 프레임 수. dt 가 아니라 프레임 수로 진행해 자동 검증에서 결정적임
	int logEvery = 60;            // 이 프레임마다 roll 을 로그로 남김 (0 = 안 남김)
	DirectX::XMFLOAT3 startPosition = DirectX::XMFLOAT3(12.0f, 4.0f, -22.0f);
	DirectX::XMFLOAT3 lookTarget = DirectX::XMFLOAT3(0.0f, 2.0f, 0.0f);

private:
	DirectX::XMFLOAT4 m_q = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);   // 누적 회전. 이 스크립트 안에서만 쿼터니언이 원본이고 Transform 은 출력임
	int m_frame = 0;
};

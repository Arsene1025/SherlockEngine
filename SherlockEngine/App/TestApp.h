#pragma once
#include "App/AppBase.h"

class TestApp : public AppBase
{
public:
    TestApp();

    virtual bool Initialize() override;
    virtual void UpdateGUI() override;
    virtual void Update(float dt) override;
    virtual void Render() override;

protected:
    virtual void OnFocusLost() override;

private:
    // WASD/QE 이동 + 우클릭 드래그 회전. ImGui가 입력을 쓰는 동안은 무시한다.
    void UpdateCamera(float dt);
    void BeginLook();
    void EndLook();   // 여러 번 불러도 안전하다 (포커스 상실·캡처 상실에서도 호출됨)

private:
    // Update()는 ImGui 프레임 밖에서 불리므로 dt를 보관했다가 UpdateGUI()에서 보여 준다.
    float m_lastDt = 0.0f;

    // 마우스 회전 상태
    bool m_lookActive = false;
    POINT m_lookAnchorScreen = {};   // 드래그 시작 시 커서 위치(화면 좌표). 매 프레임 여기로 되돌린다.
    float m_lookSensitivity = 0.0025f;   // 라디안 / 픽셀
    float m_moveSpeed = 10.0f;           // 단위 / 초. Shift로 4배.

    // dt 검증용: 두 번째 구가 첫 구 주위를 돈다. 1회전 = 2π초.
    float m_orbitAngle = 0.0f;
};

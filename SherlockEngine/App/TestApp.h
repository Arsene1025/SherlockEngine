#pragma once
#include "App/AppBase.h"
#include "Scene/Scene.h"

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
    void BuildScene();

    // WASD/QE 이동 + 우클릭 드래그 회전. ImGui가 입력을 쓰는 동안은 무시한다.
    void UpdateCamera(float dt);
    void BeginLook();
    void EndLook();   // 여러 번 불러도 안전하다 (포커스 상실·캡처 상실에서도 호출됨)

private:
    Scene m_scene;

    // 애니메이션 대상. Scene의 오브젝트 벡터는 BuildScene 이후 크기가 바뀌지 않으므로 인덱스로 든다.
    size_t m_orbitSphere = 0;
    size_t m_bobCube = 0;
    size_t m_spinCylinder = 0;
    size_t m_pulseSphere = 0;

    // Update()는 ImGui 프레임 밖에서 불리므로 dt를 보관했다가 UpdateGUI()에서 보여 준다.
    float m_lastDt = 0.0f;

    // 마우스 회전 상태
    bool m_lookActive = false;
    POINT m_lookAnchorScreen = {};   // 드래그 시작 시 커서 위치(화면 좌표). 매 프레임 여기로 되돌린다.
    float m_lookSensitivity = 0.0025f;   // 라디안 / 픽셀
    float m_moveSpeed = 10.0f;           // 단위 / 초. Shift로 4배.
};

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

private:
    // Update()는 ImGui 프레임 밖에서 불리므로 dt를 보관했다가 UpdateGUI()에서 보여 준다.
    float m_lastDt = 0.0f;
};

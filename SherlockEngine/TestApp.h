#pragma once
#include "AppBase.h"
class TestApp : public AppBase
{
public:
    TestApp();

    virtual bool Initialize() override;
    virtual void UpdateGUI() override;
    virtual void Update() override;
    virtual void Render() override;
};


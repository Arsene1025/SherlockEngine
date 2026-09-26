#include "pch.h"
#include "Core/Log.h"
#include "App/TestApp.h"

// 에디터 진입점 (SherlockEditor.exe). 11-D단계에서 SherlockEngine\main.cpp 를 여기로 옮겼음 — 엔진은 정적 라이브러리(SherlockEngine.lib),
// 에디터(TestApp·Editor·ContentBrowser·ScriptCreator·GameBuilder)는 이 프로젝트, 게임 런타임은 SherlockGame 에 있음.
//
// 10단계 (D19): Windows 서브시스템. 콘솔 창은 기본으로 열리지 않고, 명령줄에서 실행하면 Logger 가 설정에 따라 부모 콘솔에 붙음.
// 로그는 파일·OutputDebugString·ImGui 콘솔로도 가므로 콘솔이 없어도 잃는 것이 없음. Logger 초기화는 AppBase::Initialize 가 설정 파일을 읽은 뒤에 함.
#ifndef SHERLOCK_PROJECT_NAME
#define SHERLOCK_PROJECT_NAME ""   // 11-E단계: exe 프로젝트가 정의함 (엔진 솔루션 = "Sample", 생성된 프로젝트 솔루션 = 그 프로젝트 이름)
#endif

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    AppBase::SetCompiledProjectName(SHERLOCK_PROJECT_NAME);
    TestApp app;
    if (!app.Initialize())
    {
        Log::Error("App 초기화 실패!");
        return -1;
    }
    return app.Run();
}

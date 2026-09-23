#include "pch.h"
#include "Core/Log.h"
#include "App/TestApp.h"

// 10단계 (D19): Windows 서브시스템. 콘솔 창은 기본으로 열리지 않고, Logger 가 설정에 따라 부모 콘솔에
// 붙거나(명령줄에서 실행) Debug 빌드에서 하나 연다. 로그는 파일·OutputDebugString·ImGui 콘솔로도 가므로
// 콘솔이 없어도 잃는 것이 없다. Logger 초기화는 AppBase::Initialize 가 설정 파일을 읽은 뒤에 한다.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    TestApp app;
    if (!app.Initialize())
    {
        Log::Error("App 초기화 실패!");
        return -1;
    }
    return app.Run();
}

#include "pch.h"
#include "Core/Log.h"
#include "App/TestApp.h"

int main()
{
    // 콘솔 코드페이지를 UTF-8로. 다른 로그보다 먼저 불러야 한다.
    Log::Init();

    TestApp testApp;

    if (!testApp.Initialize())
    {
        Log::Error("App 초기화 실패!");
        return -1;
    }

    return testApp.Run();
}

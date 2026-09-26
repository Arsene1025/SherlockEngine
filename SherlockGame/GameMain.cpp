#include "pch.h"
#include "Core/Log.h"
#include "App/GameApp.h"

// 11-D단계: 게임 런타임 진입점 (SherlockGame.exe). 에디터(SherlockEditor.exe, EditorMain.cpp)와 같은 엔진 라이브러리 위에서 동작함.
// Windows 서브시스템 — 콘솔 없음. 로그는 exe 옆 Logs\ 폴더의 파일로 남김.
#ifndef SHERLOCK_PROJECT_NAME
#define SHERLOCK_PROJECT_NAME ""   // 11-E단계: exe 프로젝트가 정의함
#endif

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	AppBase::SetCompiledProjectName(SHERLOCK_PROJECT_NAME);
	GameApp app;
	if (!app.Initialize())
	{
		Log::Error("게임 초기화 실패!");
		return -1;
	}
	return app.Run();
}

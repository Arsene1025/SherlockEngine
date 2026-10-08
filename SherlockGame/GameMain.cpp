#include <windows.h>
#include "App/EntryPoints.h"

// 11-D단계: 게임 런타임 진입점 (SherlockGame.exe). 에디터(SherlockEditor.exe, EditorMain.cpp)와 같은 엔진 위에서 동작함.
// 2026-10-08 (B안): 엔진은 SherlockEngine.dll, 프로젝트 스크립트는 <프로젝트>Scripts.dll — GameApp 이 시작할 때 올림.
// Windows 서브시스템 — 콘솔 없음. 로그는 exe 옆 Logs\ 폴더의 파일로 남김.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	return RunGame();
}

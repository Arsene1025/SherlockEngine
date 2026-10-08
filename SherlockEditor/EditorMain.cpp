#include <windows.h>
#include "App/EntryPoints.h"

// 에디터 진입점 (SherlockEditor.exe). 11-D단계에서 SherlockEngine\main.cpp 를 여기로 옮겼음.
// 2026-10-08 (B안): 엔진과 에디터(EditorApp·Editor·ContentBrowser …)는 SherlockEngine.dll 에 있고, 이 exe 는 그것을 부르기만 함.
// 프로젝트 스크립트도 exe 에 컴파일하지 않음 — 에디터가 프로젝트를 열 때 <프로젝트>Scripts.dll 을 올림 (Core/ScriptModule.h).
// 그래서 이 exe 하나로 어떤 프로젝트든 열 수 있음.
//
// 10단계 (D19): Windows 서브시스템. 콘솔 창은 기본으로 열리지 않고, 명령줄에서 실행하면 Logger 가 설정에 따라 부모 콘솔에 붙음.
// 로그는 파일·OutputDebugString·ImGui 콘솔로도 가므로 콘솔이 없어도 잃는 것이 없음. Logger 초기화는 AppBase::Initialize 가 설정 파일을 읽은 뒤에 함.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    return RunEditor();
}

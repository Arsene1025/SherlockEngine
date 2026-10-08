#pragma once
#include "Core/EngineApi.h"

// 2026-10-08: exe 진입점 (B안 — 엔진은 SherlockEngine.dll).
//
// 에디터(EditorApp)와 게임 런타임(GameApp)은 엔진 DLL 안에 있고, 두 exe 는 wWinMain 에서 이 함수 하나만 부름.
// 에디터 소스를 exe 에 두지 않는 이유: ImGui 가 정적 라이브러리라 exe 와 DLL 이 각자 링크하면 ImGui 전역 컨텍스트가 둘이 됨.
// 프로젝트 스크립트는 exe 가 아니라 <프로젝트>Scripts.dll 에 있으므로 exe 는 어떤 프로젝트에도 묶이지 않음 — SherlockEditor.exe 하나로 모든 프로젝트를 엶.
SHERLOCK_API int RunEditor();
SHERLOCK_API int RunGame();

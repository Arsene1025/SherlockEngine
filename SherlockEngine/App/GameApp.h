#pragma once
#include "App/AppBase.h"
#include <string>

// 11-D단계: 게임 런타임. 에디터 없이 시작 씬을 로드해 바로 재생한다 — 언리얼의 패키징된 게임, 유니티의 플레이어 빌드에 해당.
//
// 같은 엔진 라이브러리 위의 두 번째 앱이다: TestApp(에디터) 은 SherlockEditor.exe, 이것은 SherlockGame.exe.
// ImGui 컨텍스트를 만들지 않으므로(WantsGUI false) 씬은 오프스크린이 아니라 백버퍼에 직접 그려진다.
// 설정 [game] startScene (Assets\Scenes 기준 파일명 또는 절대 경로), title. ESC 로 종료.
// 자동 검증: --exit-after=N, --screenshot=path, --dump-objects=1 (에디터와 같은 인자).
class GameApp : public AppBase
{
protected:
	bool WantsGUI() const override { return false; }
	const wchar_t* GetWindowTitle() const override { return m_title.c_str(); }
	bool OnInitialize() override;
	void OnUpdate(float dt) override;
	void OnFixedUpdate(float fixedDt) override;
	void OnGUI() override {}

private:
	std::wstring m_title = L"Sherlock Game";
	uint64_t m_exitAfter = 0;
	std::wstring m_screenshotPath;
	bool m_dumpObjects = false;
};

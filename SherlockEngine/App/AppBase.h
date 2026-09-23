#pragma once
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <string>
#include "Core/Engine.h"
#include "Core/Config.h"

// AppBase (10단계, D14): 창과 메시지 루프, ImGui 컨텍스트·Win32 백엔드. 엔진 시스템은 전부 Engine 이 갖는다.
//
// 앱은 이것을 상속해 OnInitialize / OnUpdate / OnFixedUpdate / OnGUI 를 구현한다. 렌더는 Engine::Render 가
// 정해진 순서로 한다 — 앱이 "무엇을" 만 쓰고 "어떤 순서로 그리는지"는 모른다.
// 전역 g_appBase 는 유지한다: WndProc 이 멤버 함수로 오는 가장 단순한 길이고 창은 하나다.
class AppBase
{
public:
    AppBase();
    virtual ~AppBase();

    // 설정 파일 → Logger → 창 → ImGui 컨텍스트 → Engine → OnInitialize. 실패하면 false.
    bool Initialize();
    int Run();

    virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:
    // ---- 앱이 구현하는 것 ----
    virtual bool OnInitialize() = 0;              // 씬 구성. Engine 은 준비되어 있다
    virtual void OnUpdate(float dt) = 0;          // 프레임마다 (가변 dt)
    virtual void OnFixedUpdate(float fixedDt) { (void)fixedDt; }   // 고정 스텝 (Time.fixedStep). 필요할 때만
    virtual void OnGUI() = 0;                     // "Information" 창 안. 다른 창을 열어도 된다
    virtual void OnFocusLost() {}                 // WM_KILLFOCUS / WM_CAPTURECHANGED

    Engine& GetEngine() { return m_engine; }
    HWND GetWindow() const { return m_mainWindow; }
    const Config& GetConfig() const { return m_config; }

    // 실행 인자 "--name=value" 의 value. 없으면 빈 문자열.
    static std::wstring GetCommandLineOption(const wchar_t* name);

private:
    bool LoadConfig();          // Assets\Config\engine.ini + 실행 인자 덮어쓰기
    void InitLogger();          // 설정의 [log] 로 Logger 를 연다
    bool InitMainWindow();
    bool InitGUI();

private:
    Config m_config;
    Engine m_engine;

    int m_screenWidth = 1280;   // 클라이언트 영역 크기 (설정 engine.width/height)
    int m_screenHeight = 720;
    HWND m_mainWindow = nullptr;

    // 초기화가 도중에 실패해도 소멸자가 안전하도록, 실제로 초기화된 것만 기록해 그만큼만 되돌린다.
    bool m_guiWin32Initialized = false;
    bool m_guiContextCreated = false;
};

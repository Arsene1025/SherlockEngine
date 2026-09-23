#pragma once
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <string>
#include "Core/Engine.h"
#include "Core/Config.h"
#include "Core/Project.h"

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

    // 11-E단계: 이 실행 파일에 컴파일된 프로젝트 이름. 열린 프로젝트와 다르면 그 프로젝트의 스크립트가 없다.
    // SHERLOCK_PROJECT_NAME 은 exe 프로젝트에만 정의되므로(엔진 lib 는 모른다) 진입점(EditorMain/GameMain)이 Initialize 전에 넣어 준다.
    static void SetCompiledProjectName(const char* name);
    static const char* GetCompiledProjectName();

protected:
    // ---- 앱이 구현하는 것 ----
    virtual bool OnInitialize() = 0;              // 씬 구성. Engine 은 준비되어 있다
    virtual void OnUpdate(float dt) = 0;          // 프레임마다 (가변 dt)
    virtual void OnFixedUpdate(float fixedDt) { (void)fixedDt; }   // 고정 스텝 (Time.fixedStep). 필요할 때만
    virtual void OnGUI() = 0;                     // "Information" 창 안. 다른 창을 열어도 된다
    virtual void OnFocusLost() {}                 // WM_KILLFOCUS / WM_CAPTURECHANGED
    // 11-D단계: false 면 ImGui 컨텍스트·Win32/렌더러 백엔드를 만들지 않는다 (게임 런타임 GameApp). OnGUI 는 불리지 않는다.
    virtual bool WantsGUI() const { return true; }
    virtual const wchar_t* GetWindowTitle() const { return L"SherlockEngine"; }

    Engine& GetEngine() { return m_engine; }
    HWND GetWindow() const { return m_mainWindow; }
    const Config& GetConfig() const { return m_config; }

    // 11-E단계: 프로젝트. Initialize 가 설정보다 먼저 정한다 (--project=, exe 위쪽의 *.sherlock, 엔진의 Projects\Sample 순).
    // OpenProject 는 실행 중 전환 (Paths 의 프로젝트 루트를 바꾼다). 씬 로드는 앱의 몫.
    Project& GetProject() { return m_project; }
    const Project& GetProject() const { return m_project; }
    bool OpenProject(const std::wstring& pathOrDir);
    static std::wstring GetDefaultProjectDir();   // <엔진 저장소>\Projects\Sample\ (소스 트리가 있을 때)


    // 실행 인자 "--name=value" 의 value. 없으면 빈 문자열.
    static std::wstring GetCommandLineOption(const wchar_t* name);

private:
    void ResolveProject();      // 11-E단계: 프로젝트 파일 찾기 → Paths 루트 설정
    bool LoadConfig();          // Assets\Config\engine.ini + 실행 인자 덮어쓰기
    void InitLogger();          // 설정의 [log] 로 Logger 를 연다
    bool InitMainWindow();
    bool InitGUI();

private:
    Config m_config;
    Engine m_engine;
    Project m_project;   // 11-E단계

    int m_screenWidth = 1280;   // 클라이언트 영역 크기 (설정 engine.width/height)
    int m_screenHeight = 720;
    HWND m_mainWindow = nullptr;

    // 초기화가 도중에 실패해도 소멸자가 안전하도록, 실제로 초기화된 것만 기록해 그만큼만 되돌린다.
    bool m_guiWin32Initialized = false;
    bool m_guiContextCreated = false;
    bool m_comInitialized = false;   // 11-D단계: CoInitializeEx (WIC)
};

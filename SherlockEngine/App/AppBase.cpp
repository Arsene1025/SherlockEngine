#include "pch.h"
#include "App/AppBase.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include "Core/Profiler.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <ImGuizmo.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <shellapi.h>   // CommandLineToArgvW

using namespace DirectX;   // 이 파일 안에서만

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

AppBase* g_appBase = nullptr;
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 소멸 중이거나 아직 생성 전이면 g_appBase가 없음. DestroyWindow가 보내는 WM_DESTROY도 이 경로로 들어옴.
    if (g_appBase == nullptr)
    {
        return ::DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return g_appBase->MsgProc(hWnd, msg, wParam, lParam);
}

AppBase::AppBase()
{
    g_appBase = this;
}

AppBase::~AppBase()
{
    // 창을 먼저 파괴함. DestroyWindow는 WM_DESTROY를 동기적으로 보내므로 이때 g_appBase가 아직 살아 있어야 함.
    if (m_mainWindow != nullptr)
    {
        DestroyWindow(m_mainWindow);
        m_mainWindow = nullptr;
    }
    g_appBase = nullptr;

    // 초기화한 것만 초기화의 역순으로 정리함: Engine(Device·ImGui 렌더러) → ImGui Win32 → ImGui 컨텍스트 → Logger.
    m_engine.Shutdown();
    if (m_guiWin32Initialized)
    {
        ImGui_ImplWin32_Shutdown();
    }
    if (m_guiContextCreated)
    {
        ImGui::DestroyContext();
    }
    Log::Info("종료.");
    Log::Shutdown();
    if (m_comInitialized) CoUninitialize();
}

std::wstring AppBase::GetCommandLineOption(const wchar_t* name)
{
    std::wstring result;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) return result;
    const std::wstring prefix = std::wstring(L"--") + name + L"=";
    for (int i = 1; i < argc; ++i)
    {
        if (_wcsnicmp(argv[i], prefix.c_str(), prefix.size()) == 0)
        {
            result = argv[i] + prefix.size();
        }
    }
    LocalFree(argv);
    return result;
}

namespace
{
    std::string s_compiledProjectName;
}

void AppBase::SetCompiledProjectName(const char* name)
{
    s_compiledProjectName = name ? name : "";
}

const char* AppBase::GetCompiledProjectName()
{
    return s_compiledProjectName.c_str();
}

std::wstring AppBase::GetDefaultProjectDir()
{
    if (!Paths::HasEngineSourceTree()) return L"";
    // 엔진 루트 = <repo>\SherlockEngine 폴더 → <repo>\Projects\Sample 폴더 (주석 끝의 백슬래시는 줄 연속으로 처리되므로 피함)
    const std::wstring engine = Paths::GetEngineRoot();
    const size_t slash = engine.find_last_of(L'\\', engine.size() - 2);
    if (slash == std::wstring::npos) return L"";
    return engine.substr(0, slash + 1) + L"Projects\\Sample\\";
}

void AppBase::ResolveProject()
{
    std::wstring path = GetCommandLineOption(L"project");
    if (path.empty()) path = Project::FindUpwards(Paths::GetExecutableDir(), 3);   // Binaries\Debug\ 에서는 프로젝트 폴더를, 배포 폴더에서는 같은 폴더의 .sherlock 을 찾음
    if (path.empty()) path = GetDefaultProjectDir();
    if (path.empty()) return;
    OpenProject(path);
}

bool AppBase::OpenProject(const std::wstring& pathOrDir)
{
    Project project;
    if (!project.Load(pathOrDir)) return false;
    m_project = project;
    Paths::SetEngineRoot(m_project.engineRoot);
    Paths::SetProjectRoot(m_project.GetRoot());
    return true;
}

bool AppBase::LoadConfig()
{
    // Logger 보다 먼저 실행됨 (로그 파일 경로를 여기서 읽음). 그래서 이 함수는 로그를 남기지 않고 결과만 돌려줌.
    const std::wstring path = Paths::GetAssetPath(L"Config\\engine.ini");
    const bool loaded = m_config.LoadFromFile(path);

    // 실행 인자로 덮어쓰기: --backend=, --scene= 는 8~9단계의 이름을 그대로 쓰고, 그 밖에는 일반형 --section.key=value 를 씀.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv != nullptr)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring arg = argv[i];
            if (arg.size() < 3 || arg[0] != L'-' || arg[1] != L'-') continue;
            const size_t equals = arg.find(L'=');
            if (equals == std::wstring::npos) continue;
            std::string key = Log::ToUtf8(arg.substr(2, equals - 2).c_str());
            const std::string value = Log::ToUtf8(arg.substr(equals + 1).c_str());
            if (key == "backend") key = "engine.backend";
            else if (key == "scene") key = "engine.scene";
            m_config.Set(key, value);
        }
        LocalFree(argv);
    }
    m_screenWidth = m_config.GetInt("engine.width", 1280);
    m_screenHeight = m_config.GetInt("engine.height", 720);
    if (m_screenWidth < 64) m_screenWidth = 64;
    if (m_screenHeight < 64) m_screenHeight = 64;
    return loaded;
}

void AppBase::InitLogger()
{
    Log::InitDesc desc;
    desc.console = m_config.GetBool("log.console", true);
    // D19: Windows 서브시스템이라 콘솔이 없음. 예전에는 Debug 에서 콘솔을 하나 열었지만(10단계) 11단계부터는 에디터의 Console 창과 로그 파일이 있으므로
    // 검은 콘솔 창은 띄우지 않음. 명령줄에서 실행하면 여전히 부모 콘솔에 붙음. 필요하면 engine.ini 에서 log.allocConsole = true 로 설정.
    desc.allocConsole = m_config.GetBool("log.allocConsole", false);
    const std::string file = m_config.GetString("log.file", "Logs\\SherlockEngine.log");
    if (!file.empty())
    {
        std::wstring wide(file.begin(), file.end());   // 경로는 ASCII 라고 가정 (설정 파일에 우리가 넣은 값)
        for (wchar_t& c : wide) if (c == L'/') c = L'\\';
        desc.filePath = Paths::GetExecutableDir() + wide;
    }
    Log::Level level = Log::Level::Info;
    if (Log::ParseLevel(m_config.GetString("log.level", "info"), level)) desc.minLevel = level;
    Log::Init(desc);
}

bool AppBase::Initialize()
{
    // 11-D단계: COM 을 명시적으로 초기화함. DirectXTex 의 WIC(PNG/JPG 읽기, 스크린샷 저장)가 COM 팩토리를 만들기 때문임.
    // 에디터에서는 ImGui 쪽에서 COM 이 우연히 초기화되어 동작했지만, 게임 런타임(ImGui 없음)에서는 텍스처 로드가 E_NOINTERFACE 로 실패했음.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    m_comInitialized = SUCCEEDED(com);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) Log::Warn("CoInitializeEx 실패 (0x%08X)", static_cast<unsigned>(com));

    ResolveProject();   // 11-E단계: 설정(engine.ini)보다 먼저 호출 — 에셋 루트가 여기서 정해짐
    const bool configLoaded = LoadConfig();
    InitLogger();
    if (m_project.IsLoaded())
    {
        Log::Info("프로젝트 루트: %s (엔진 루트 %s%s, 컴파일된 프로젝트 %s)", Log::ToUtf8(Paths::GetProjectRoot().c_str()).c_str(),
            Log::ToUtf8(Paths::GetEngineRoot().c_str()).c_str(), Paths::HasEngineSourceTree() ? "" : ", 배포", GetCompiledProjectName());
    }
    else Log::Info("프로젝트 없음: 엔진 콘텐츠만 (%s)", Log::ToUtf8(Paths::GetEngineAssetRoot().c_str()).c_str());
    if (configLoaded) Log::Info("설정 파일: %s (%zu 항목)", Log::ToUtf8(m_config.GetPath().c_str()).c_str(), m_config.GetAll().size());
    else Log::Warn("설정 파일 Assets\\Config\\engine.ini 를 찾지 못해 기본값으로 실행.");
    if (!m_config.GetErrors().empty()) Log::Warn("설정 파일 파싱 오류:\n%s", m_config.GetErrors().c_str());

    if (!InitMainWindow()) return false;

    // ImGui 컨텍스트는 Engine 보다 먼저 만듦 (Engine 이 렌더러 백엔드를 붙이기 때문). Win32 백엔드는 창이 이미 있으므로 지금 초기화해도 됨.
    // 11-D단계: 게임 런타임(WantsGUI false)은 컨텍스트를 만들지 않음 → Engine 도 ImGui 렌더러 백엔드를 건너뜀.
    if (WantsGUI() && !InitGUI()) return false;

    Engine::Desc desc;
    desc.windowHandle = m_mainWindow;
    desc.width = m_screenWidth;
    desc.height = m_screenHeight;
    if (!m_engine.Initialize(m_config, desc)) return false;

    if (WantsGUI())
    {
        if (!ImGui_ImplWin32_Init(m_mainWindow))
        {
            Log::Error("ImGui Win32 백엔드 초기화 실패");
            return false;
        }
        m_guiWin32Initialized = true;
    }

    if (!OnInitialize()) return false;

    // 11단계: 자동 검증 모드(--exit-after)에서는 포커스를 가져오지 않음. 사용자가 다른 창(게임 등)을 쓰는 중일 수 있기 때문임.
    if (GetCommandLineOption(L"exit-after").empty()) SetForegroundWindow(m_mainWindow);
    return true;
}

int AppBase::Run()
{
    MSG msg = { 0 };
    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        // 시간·프로파일러·ImGui 렌더러 프레임 시작. ImGui 프레임은 Update보다 먼저 엶:
        // io.WantCaptureMouse/Keyboard 는 ImGui::NewFrame 에서 갱신되므로, 그 뒤에 Update 가 읽어야 "이번 프레임" 값을 얻음.
        const float dt = m_engine.BeginFrame();
        const bool gui = m_guiWin32Initialized;   // 11-D단계: 게임 런타임에는 ImGui 프레임이 없음
        if (gui)
        {
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            ImGuizmo::BeginFrame();   // 11단계
        }

        {
            ProfileScope scope("Update");
            Time& time = m_engine.GetTime();
            while (time.ConsumeFixedStep())
            {
                OnFixedUpdate(time.GetFixedStep());
            }
            OnUpdate(dt);
        }

        if (gui)
        {
            ProfileScope scope("GUI");
            OnGUI();   // 11단계: 앱(에디터)이 창을 직접 만듦. 도킹 공간도 앱에서 만듦.
            ImGui::Render();
        }

        m_engine.Render();
        m_engine.EndFrame();
    }
    return 0;
}

LRESULT AppBase::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // ImGui Win32 백엔드가 준비되기 전이나 종료된 뒤에는 메시지를 넘기면 안 됨.
    if (m_guiWin32Initialized && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
    {
        return true;
    }
    Input& input = m_engine.GetInput();
    switch (msg)
    {
        // ---- 입력 공급. ImGui가 소비하는지와 무관하게 항상 넣음 (Input.h 참고). ----
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:   // Alt 조합. break 뒤 DefWindowProc로 넘어가야 Alt+F4가 동작함.
            input.OnKeyDown(static_cast<uint32_t>(wParam));
            // F10 은 혼자 눌러도 WM_SYSKEYDOWN 으로 오고, DefWindowProc 가 메뉴 모드에 들어가 다음 키 입력을 삼킴.
            if (wParam == VK_F10) return 0;
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            input.OnKeyUp(static_cast<uint32_t>(wParam));
            if (wParam == VK_F10) return 0;
            break;
        case WM_LBUTTONDOWN: input.OnMouseButton(MouseButton::Left, true);    break;
        case WM_LBUTTONUP:   input.OnMouseButton(MouseButton::Left, false);   break;
        case WM_RBUTTONDOWN: input.OnMouseButton(MouseButton::Right, true);   break;
        case WM_RBUTTONUP:   input.OnMouseButton(MouseButton::Right, false);  break;
        case WM_MBUTTONDOWN: input.OnMouseButton(MouseButton::Middle, true);  break;
        case WM_MBUTTONUP:   input.OnMouseButton(MouseButton::Middle, false); break;
        case WM_MOUSEMOVE:
            // LOWORD/HIWORD가 아니라 GET_X_LPARAM 을 씀. 캡처 중에는 창 밖 좌표가 음수로 오기 때문임.
            input.OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        case WM_MOUSEWHEEL:
            input.OnMouseWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA);
            break;
        case WM_KILLFOCUS:
            // Alt-Tab 중에 키를 떼면 KeyUp이 오지 않으므로 입력 상태를 전부 초기화함.
            input.OnFocusLost();
            OnFocusLost();
            break;
        case WM_CAPTURECHANGED:
            // lParam은 캡처를 새로 얻은 창임. 자기 자신이면 캡처 상실이 아님 (BeginLook 의 SetCapture).
            if (reinterpret_cast<HWND>(lParam) != hwnd)
            {
                input.OnCaptureLost();
                OnFocusLost();
            }
            break;

        case WM_SIZE:
            // 엔진 초기화 전(맨 처음 시작할 때)에는 Resize를 호출하지 않음
            if (m_engine.IsInitialized() && wParam != SIZE_MINIMIZED)
            {
                m_engine.OnResize(LOWORD(lParam), HIWORD(lParam));
            }
            break;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

bool AppBase::InitMainWindow()
{
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL),
                      NULL, NULL, NULL, NULL, L"SherlockEngine", NULL };
    if (!RegisterClassEx(&wc))
    {
        Log::Error("RegisterClassEx() 실패");
        return false;
    }

    // 클라이언트 영역이 width x height 가 되도록 창 크기를 다시 계산함.
    RECT wr = { 0, 0, m_screenWidth, m_screenHeight };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, false);

    // 11단계: 자동 검증 모드에서는 창을 화면 밖에 비활성 상태로 만듦 — 스크린샷은 Device 리드백으로 찍으므로 창이 보일 필요가 없음.
    const bool automation = !GetCommandLineOption(L"exit-after").empty();
    const int windowX = automation ? -3000 : 100;
    m_mainWindow = CreateWindow(wc.lpszClassName, GetWindowTitle(), WS_OVERLAPPEDWINDOW,
        windowX, 100, wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, wc.hInstance, NULL);
    if (!m_mainWindow)
    {
        Log::Error("CreateWindow() 실패");
        return false;
    }
    ShowWindow(m_mainWindow, automation ? SW_SHOWNOACTIVATE : SW_SHOWDEFAULT);
    UpdateWindow(m_mainWindow);
    return true;
}

bool AppBase::InitGUI()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    m_guiContextCreated = true;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // 11단계: 에디터 창 도킹
    ImGui::StyleColorsLight();

    // 10단계: 로그 콘솔에 한글을 표시해야 하므로 시스템의 맑은 고딕을 추가함. 없으면 기본 폰트(ASCII 만)를 씀.
    // ImGui 1.92 는 글리프를 필요할 때 만들므로 글리프 범위를 미리 지정하지 않아도 됨.
    if (GetFileAttributesW(L"C:/Windows/Fonts/malgun.ttf") != INVALID_FILE_ATTRIBUTES)
    {
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        if (io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/malgun.ttf", 15.0f, &fontConfig) == nullptr)
        {
            Log::Warn("맑은 고딕 폰트를 읽지 못해 ImGui 기본 폰트를 쓴다 (한글은 ? 로 보인다).");
        }
    }
    return true;
}

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
    // 소멸 중이거나 아직 생성 전이면 g_appBase가 없다. DestroyWindow가 보내는 WM_DESTROY도 이 경로로 온다.
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
    // 창을 먼저 파괴한다. DestroyWindow는 WM_DESTROY를 동기적으로 보내므로 g_appBase가 아직 살아 있어야 한다.
    if (m_mainWindow != nullptr)
    {
        DestroyWindow(m_mainWindow);
        m_mainWindow = nullptr;
    }
    g_appBase = nullptr;

    // 초기화한 것만, 초기화의 역순으로: Engine(Device·ImGui 렌더러) → ImGui Win32 → ImGui 컨텍스트 → Logger.
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

bool AppBase::LoadConfig()
{
    // Logger 보다 먼저다 (로그 파일 경로가 여기서 나온다). 그래서 이 함수는 로그를 남기지 않고 결과만 돌려준다.
    const std::wstring path = Paths::GetAssetPath(L"Config\\engine.ini");
    const bool loaded = m_config.LoadFromFile(path);

    // 실행 인자 덮어쓰기: --backend=, --scene= 는 8~9단계의 이름 그대로, 그 밖의 --section.key=value 는 일반형.
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
#ifdef _DEBUG
    desc.allocConsole = true;    // D19: Windows 서브시스템이라 콘솔이 없다. Debug 에서만 하나 연다
#else
    desc.allocConsole = false;
#endif
    const std::string file = m_config.GetString("log.file", "Logs\\SherlockEngine.log");
    if (!file.empty())
    {
        std::wstring wide(file.begin(), file.end());   // 경로는 ASCII 라고 가정 (설정 파일의 우리 값)
        for (wchar_t& c : wide) if (c == L'/') c = L'\\';
        desc.filePath = Paths::GetExecutableDir() + wide;
    }
    Log::Level level = Log::Level::Info;
    if (Log::ParseLevel(m_config.GetString("log.level", "info"), level)) desc.minLevel = level;
    Log::Init(desc);
}

bool AppBase::Initialize()
{
    const bool configLoaded = LoadConfig();
    InitLogger();
    if (configLoaded) Log::Info("설정 파일: %s (%zu 항목)", Log::ToUtf8(m_config.GetPath().c_str()).c_str(), m_config.GetAll().size());
    else Log::Warn("설정 파일 Assets\\Config\\engine.ini 를 찾지 못해 기본값으로 실행.");
    if (!m_config.GetErrors().empty()) Log::Warn("설정 파일 파싱 오류:\n%s", m_config.GetErrors().c_str());

    if (!InitMainWindow()) return false;

    // ImGui 컨텍스트는 Engine 보다 먼저 (Engine 이 렌더러 백엔드를 붙인다), Win32 백엔드는 창이 있으니 지금.
    if (!InitGUI()) return false;

    Engine::Desc desc;
    desc.windowHandle = m_mainWindow;
    desc.width = m_screenWidth;
    desc.height = m_screenHeight;
    if (!m_engine.Initialize(m_config, desc)) return false;

    if (!ImGui_ImplWin32_Init(m_mainWindow))
    {
        Log::Error("ImGui Win32 백엔드 초기화 실패");
        return false;
    }
    m_guiWin32Initialized = true;

    if (!OnInitialize()) return false;

    // 11단계: 자동 검증 모드(--exit-after)에서는 포커스를 가져오지 않는다. 사용자가 다른 창(게임 등)을 쓰는 중일 수 있다.
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

        // 시간·프로파일러·ImGui 렌더러 프레임. ImGui 프레임을 Update보다 먼저 연다:
        // io.WantCaptureMouse/Keyboard 는 ImGui::NewFrame 에서 갱신되므로 그 뒤에 Update 가 읽어야 "이번 프레임" 값을 본다.
        const float dt = m_engine.BeginFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();   // 11단계

        {
            ProfileScope scope("Update");
            Time& time = m_engine.GetTime();
            while (time.ConsumeFixedStep())
            {
                OnFixedUpdate(time.GetFixedStep());
            }
            OnUpdate(dt);
        }

        {
            ProfileScope scope("GUI");
            OnGUI();   // 11단계: 앱(에디터)이 창을 직접 만든다. 도킹 공간도 거기서.
            ImGui::Render();
        }

        m_engine.Render();
        m_engine.EndFrame();
    }
    return 0;
}

LRESULT AppBase::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // ImGui Win32 백엔드가 준비되기 전이나 종료된 뒤에는 넘기면 안 된다.
    if (m_guiWin32Initialized && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
    {
        return true;
    }
    Input& input = m_engine.GetInput();
    switch (msg)
    {
        // ---- 입력 공급. ImGui가 소비하는지와 무관하게 항상 넣는다 (Input.h 참고). ----
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:   // Alt 조합. break 뒤 DefWindowProc로 흘러야 Alt+F4가 동작한다.
            input.OnKeyDown(static_cast<uint32_t>(wParam));
            // F10 은 혼자 눌러도 WM_SYSKEYDOWN 으로 오고, DefWindowProc 가 메뉴 모드에 들어가 다음 키 입력을 삼킨다.
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
            // LOWORD/HIWORD가 아니라 GET_X_LPARAM. 캡처 중에는 창 밖 좌표가 음수로 온다.
            input.OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        case WM_MOUSEWHEEL:
            input.OnMouseWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA);
            break;
        case WM_KILLFOCUS:
            // Alt-Tab 중에 키를 떼면 KeyUp이 오지 않는다. 전부 초기화한다.
            input.OnFocusLost();
            OnFocusLost();
            break;
        case WM_CAPTURECHANGED:
            // lParam은 캡처를 새로 얻은 창이다. 자기 자신이면 상실이 아니다 (BeginLook 의 SetCapture).
            if (reinterpret_cast<HWND>(lParam) != hwnd)
            {
                input.OnCaptureLost();
                OnFocusLost();
            }
            break;

        case WM_SIZE:
            // 맨 처음 시작때는 Resize호출하지 않기
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

    // 클라이언트 영역이 width x height 가 되도록 창 크기를 다시 계산한다.
    RECT wr = { 0, 0, m_screenWidth, m_screenHeight };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, false);

    // 11단계: 자동 검증 모드에서는 창을 화면 밖에, 활성화 없이 만든다 — 스크린샷은 Device 리드백으로 찍으므로 보일 필요가 없다.
    const bool automation = !GetCommandLineOption(L"exit-after").empty();
    const int windowX = automation ? -3000 : 100;
    m_mainWindow = CreateWindow(wc.lpszClassName, L"SherlockEngine", WS_OVERLAPPEDWINDOW,
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

    // 10단계: 로그 콘솔이 한글을 보여야 하므로 시스템의 맑은 고딕을 얹는다. 없으면 기본 폰트(ASCII 만).
    // ImGui 1.92 는 글리프를 필요할 때 만들므로 글리프 범위를 미리 주지 않아도 된다.
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

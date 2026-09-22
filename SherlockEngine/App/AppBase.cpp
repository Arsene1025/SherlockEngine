#include "pch.h"
#include "App/AppBase.h"
#include "Core/Log.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

using namespace DirectX;   // 이 파일 안에서만



extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam);

AppBase* g_appBase = nullptr;
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 소멸 중이거나 아직 생성 전이면 g_appBase가 없다.
    // DestroyWindow가 보내는 WM_DESTROY도 이 경로로 온다.
    if (g_appBase == nullptr)
    {
        return ::DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return g_appBase->MsgProc(hWnd, msg, wParam, lParam);
}

AppBase::AppBase() : m_screenWidth(1280), m_screenHeight(720), m_mainWindow(0)
{
	g_appBase = this;
}

AppBase::~AppBase()
{
	// 창을 먼저 파괴한다. DestroyWindow는 WM_DESTROY를 동기적으로 보내므로
	// g_appBase가 아직 살아 있어야 한다.
	if (m_mainWindow != nullptr)
	{
		DestroyWindow(m_mainWindow);
		m_mainWindow = nullptr;
	}

	g_appBase = nullptr;

	// 초기화한 것만, 초기화의 역순으로 되돌린다.
	if (m_guiInitialized)
	{
		ImGui_ImplDX11_Shutdown();
	}
	if (m_guiWin32Initialized)
	{
		ImGui_ImplWin32_Shutdown();
	}
	if (m_guiContextCreated)
	{
		ImGui::DestroyContext();
	}
}

float AppBase::GetAspectRatio() const
{
	return float(m_screenWidth - m_guiWidth) / m_screenHeight;
}

int AppBase::Run()
{
    // Main message loop
    MSG msg = { 0 };
    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            // 시간은 여기서 한 번만 읽고 dt로 넘긴다. 예전의 static GameTime
            // 멤버는 정의가 없어 링크가 안 됐고, 지역 변수는 프레임 끝에
            // 사라져 아무도 읽지 못했다.
            gameTimer.Tick();
            const float dt = gameTimer.DeltaTime();
            m_totalTime = gameTimer.TotalTime();

            // ImGui 프레임을 Update보다 먼저 연다. io.WantCaptureMouse/Keyboard는
            // ImGui::NewFrame에서 갱신되므로, 그 뒤에 Update가 읽어야 "이번 프레임"
            // 값을 본다. 반대 순서면 한 프레임 늦은 값으로 판단한다.
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame(); //Imgui 렌더링 시작

            Update(dt);

            ImGui::Begin("Information");

            //Imgui에서 자체적으로 프레임 계산함
            ImGui::Text
            (
                "Average %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate,
                ImGui::GetIO().Framerate
            );

            UpdateGUI(); //GUI추가하려면 여기에

            m_guiWidth = 0;
            // 화면을 크게 쓰기 위해 기능 정지
            // ImGui::SetWindowPos(ImVec2(0.0f, 0.0f));
            // m_guiWidth = int(ImGui::GetWindowWidth());

            ImGui::End();
            ImGui::Render();

            graphicsDevice.Clear();
            Render(); //실제 렌더링
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); // GUI 렌더링
            //ImGui RenderDrawData() 다음에 Present() 호출 해야함
            graphicsDevice.Present();

            // 프레임 끝. Pressed/Released 판정용 이전 상태를 넘기고 마우스 델타를 비운다.
            input.EndFrame();
        }
    }

    return 0;
}

bool AppBase::Initialize()
{
    if (!InitMainWindow()) return false;

    if (!InitDevice()) return false;
    camera.SetLens(XM_PIDIV4, GetAspectRatio(), 0.1f, 1000.0f);

    if (!InitShaderClass()) return false;
    if (!shaderClass.ShaderCreate())
    {
        Log::Error("셰이더 생성 실패");
        return false;
    }

    if (!InitRenderer()) return false;
    if (!renderer.DataLoading())
    {
        Log::Error("렌더 데이터 로드 실패");
        return false;
    }
    
    if (!InitGUI()) return false;



    gameTimer.Reset();
    gameTimer.Start();

    SetForegroundWindow(m_mainWindow);
    return true;
}

LRESULT AppBase::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // ImGui Win32 백엔드가 준비되기 전이나 종료된 뒤에는 넘기면 안 된다.
    if (m_guiWin32Initialized &&
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
    {
        return true;
    }
    switch (msg)
    {
        // ---- 입력 공급. ImGui가 소비하는지와 무관하게 항상 넣는다 (Input.h 참고). ----
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:   // Alt 조합. break 뒤 DefWindowProc로 흘러야 Alt+F4가 동작한다.
            input.OnKeyDown(static_cast<uint32_t>(wParam));
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            input.OnKeyUp(static_cast<uint32_t>(wParam));
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
            // lParam은 캡처를 새로 얻은 창이다. 이미 캡처를 가진 창에서 SetCapture를
            // 다시 부르면(ImGui 백엔드가 버튼 다운에서 먼저 잡고, 우리가 BeginLook에서
            // 또 잡는다) 자기 자신에게도 이 메시지가 온다. 그것은 상실이 아니다.
            if (reinterpret_cast<HWND>(lParam) != hwnd)
            {
                // 다른 창이 캡처를 가져갔다. ButtonUp이 오지 않을 수 있으므로 버튼 상태만 비운다.
                input.OnCaptureLost();
                OnFocusLost();
            }
            break;

        case WM_SIZE:
            //맨 처음 시작때는 Resize호출하지 않기
            if (graphicsDevice.GetSwapChain())
            {
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);

                if (wParam != SIZE_MINIMIZED)
                {
                    graphicsDevice.Resize(width, height);
                    camera.SetAspectRatio(float(width) / float(height));
                }
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
    WNDCLASSEX wc = { sizeof(WNDCLASSEX),
                     CS_CLASSDC,
                     WndProc,
                     0L,
                     0L,
                     GetModuleHandle(NULL),
                     NULL,
                     NULL,
                     NULL,
                     NULL,
                     L"SherlockEngine",
                     NULL };


    // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerclassa?redirectedfrom=MSDN
    if (!RegisterClassEx(&wc)) 
    {
        Log::Error("RegisterClassEx() 실패");
        return false;
    }

    // 툴바까지 포함한 윈도우 전체 해상도가 아니라
    // 우리가 실제로 그리는 해상도가 width x height가 되려면
    // 윈도우를 만들 해상도를 다시 계산해서 CreateWindow()에서 사용해야함
    // 렌더가 그려질 크기
    RECT wr = { 0, 0, m_screenWidth, m_screenHeight };

    // 필요한 윈도우 크기(해상도) 계산
    // wr의 값이 바뀜
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, false);

    // 윈도우를 만들때 위에서 계산한 wr 사용
    m_mainWindow = CreateWindow
    (
        wc.lpszClassName, 
        L"SherlockEngine",
        WS_OVERLAPPEDWINDOW,
        100, // 윈도우 좌측 상단의 x 좌표
        100, // 윈도우 좌측 상단의 y 좌표
        wr.right - wr.left, // 윈도우 가로 방향 해상도
        wr.bottom - wr.top, // 윈도우 세로 방향 해상도
        NULL, NULL, wc.hInstance, NULL
    );

    if (!m_mainWindow) 
    {
        Log::Error("CreateWindow() 실패");
        return false;
    }

    ShowWindow(m_mainWindow, SW_SHOWDEFAULT);
    UpdateWindow(m_mainWindow);

    return true;
}

bool AppBase::InitDevice()
{
    if (!graphicsDevice.InitDevice(m_mainWindow, m_screenWidth, m_screenHeight))
    {
        Log::Error("Device 초기화 실패");
        return false;
    }
    return true;
}

bool AppBase::InitRenderer()
{
    if (!renderer.Initialize(&graphicsDevice, &shaderClass, &camera))
    {
        Log::Error("Renderer 초기화 실패");
        return false;
    }
    return true;
}

bool AppBase::InitShaderClass()
{
    if (!shaderClass.Initialize(&graphicsDevice))
    {
        Log::Error("Shader 클래스 초기화 실패");
        return false;
    }
    return true;
}

bool AppBase::InitGUI()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    m_guiContextCreated = true;
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.DisplaySize = ImVec2(float(m_screenWidth), float(m_screenHeight));
    ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    if (!ImGui_ImplDX11_Init(graphicsDevice.GetDevice(), graphicsDevice.GetContext()))
    {
        Log::Error("ImGui DX11 백엔드 초기화 실패");
        return false;
    }
    m_guiInitialized = true;
    if (!ImGui_ImplWin32_Init(m_mainWindow))
    {
        Log::Error("ImGui Win32 백엔드 초기화 실패");
        return false;
    }
    m_guiWin32Initialized = true;
    return true;
}




#include "pch.h"
#include "App/AppBase.h"
#include "Core/Log.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

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

            Update(dt);


            graphicsDevice.Clear();


            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();

            ImGui::NewFrame(); //Imgui 렌더링 시작
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

            Render(); //실제 렌더링
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); // GUI 렌더링
            //ImGui RenderDrawData() 다음에 Present() 호출 해야함
            graphicsDevice.Present();
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




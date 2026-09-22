#pragma once
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include "Graphics/D3D11/Device.h"
#include "Graphics/Renderer.h"
#include "Graphics/D3D11/Shader.h"
#include "Scene/Camera.h"
#include "Core/GameTimer.h"   // 값 멤버라 완전한 타입 필요


class AppBase
{
    public:
        AppBase();
        virtual ~AppBase();

        float GetAspectRatio() const;

        int Run();

        virtual bool Initialize();
        virtual void UpdateGUI() = 0;
        // dt는 직전 프레임에서 이 프레임까지의 시간(초). Run()이 GameTimer에서
        // 읽어 넘긴다. 프레임 속도와 무관한 움직임은 전부 이 값에 비례시킨다.
        virtual void Update(float dt) = 0;
        virtual void Render() = 0;

        virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    protected: // 상속 받은 클래스에서도 접근 가능
        bool InitMainWindow();
        bool InitDevice();
        bool InitRenderer();
        bool InitGUI();
        bool InitShaderClass();

        // Reset() 이후 누적 시간(초). 셰이더의 time 상수 등에 쓴다.
        float m_totalTime = 0.0f;

    public:
        Device graphicsDevice;
        Renderer renderer;
        Shader shaderClass;
        Camera camera;
        GameTimer gameTimer;


        int m_screenWidth; // 렌더링할 최종 화면의 해상도
        int m_screenHeight;
        int m_guiWidth = 0;
        HWND m_mainWindow;

        // 초기화가 도중에 실패해도 소멸자가 안전하도록,
        // 실제로 초기화된 것만 기록해 그만큼만 되돌린다.
        bool m_guiInitialized = false;
        bool m_guiWin32Initialized = false;
        bool m_guiContextCreated = false;
};
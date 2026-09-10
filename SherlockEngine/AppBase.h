#pragma once
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include "Device.h"
#include "Renderer.h"
#include "Shader.h"
#include "Camera.h"
#include "GameTimer.h"   // 값 멤버라 완전한 타입 필요
#include "struct.h"      // GameTime


class AppBase 
{
    public:
        AppBase();
        virtual ~AppBase();

        float GetAspectRatio() const;

        int Run();

        virtual bool Initialize();
        virtual void UpdateGUI() = 0;
        virtual void Update() = 0;
        virtual void Render() = 0;

        virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    protected: // 상속 받은 클래스에서도 접근 가능
        bool InitMainWindow();
        bool InitDevice();
        bool InitRenderer();
        bool InitGUI();
        bool InitShaderClass();

        void SetViewport();
        bool CreateRenderTargetView();

    public:
        static GameTime gameTime;

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
        



        D3D11_VIEWPORT m_screenViewport;
};
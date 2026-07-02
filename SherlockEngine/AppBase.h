#pragma once
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include "Device.h"

class AppBase 
{
    public:
        AppBase();
        virtual ~AppBase();

        float GetAspectRatio() const;

        int Run();

        virtual bool Initialize();
        virtual void UpdateGUI() = 0;
        virtual void Update(float dt) = 0;
        virtual void Render() = 0;

        virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    protected: // 상속 받은 클래스에서도 접근 가능
        bool InitMainWindow();
        bool InitDevice();
        bool InitGUI();

        void SetViewport();
        bool CreateRenderTargetView();

    public:
        Device m_device;

        int m_screenWidth; // 렌더링할 최종 화면의 해상도
        int m_screenHeight;
        int m_guiWidth = 0;
        HWND m_mainWindow;
        

        /*ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D11RenderTargetView> renderTargetView;
        ComPtr<IDXGISwapChain> swapChain;*/


        D3D11_VIEWPORT m_screenViewport;
};
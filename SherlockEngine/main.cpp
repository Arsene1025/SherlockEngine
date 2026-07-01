#include "pch.h"

//지금은 imgui필요 없음
//#include <imgui.h>
//#include <imgui_impl_dx11.h>
//#include <imgui_impl_win32.h> 

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

//디버깅 하려면 콘솔창 있는 버전으로
//int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
//{
//
//	return 0;
//}

int main()
{
	//화면 넓이
	int width = 1280, height = 960;

    WNDCLASSEX wc = 
    { 
        sizeof(WNDCLASSEX),
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
        NULL 
    };
    RegisterClassEx(&wc);

    //그림이 그려질 부분
    RECT wr = { 0, 0, width, height };

    //필요한 윈도우의 크기를 계산하기
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    //위에서 구한 wr로 윈도우를 생성하기
    HWND hwnd = CreateWindow
    (
        wc.lpszClassName, 
        L"SherlockEngine Test",
        WS_OVERLAPPEDWINDOW,
        100, // 윈도우 좌측 상단의 x 좌표
        100, // 윈도우 좌측 상단의 y 좌표
        wr.right - wr.left, // 윈도우 가로 방향 해상도
        wr.bottom - wr.top, // 윈도우 세로 방향 해상도
        NULL, NULL, wc.hInstance, NULL
    );

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    //여기서 App만들어서 루프돌리기



    //

    //Main 메시지 루프
    MSG msg = {};
    while (WM_QUIT != msg.message) 
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) 
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else 
        {


            //Update -> Render -> Present
        }
    }

    //여기서 Cleanup
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{

    switch (msg) 
    {
    case WM_KEYDOWN:
        switch (wParam)
        {
        case VK_ESCAPE:
            SendMessage(hWnd, WM_DESTROY, 0, 0);
            break;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}
#pragma once


class Device
{
public:
    bool InitDevice(HWND hWnd, int width, int height);
    void ReleaseDevice();

    void Clear();
    void Present();
    void Resize(int width, int height);

    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    IDXGISwapChain* GetSwapChain() const { return m_swapChain.Get(); }

private:
    bool InitDirect3D();
    bool CreateRenderTargetView();
    void SetViewport();

public:
    UINT m_numQualityLevels = 0;

private:
    HWND m_mainWindow = nullptr;
    int m_screenWidth = 0;
    int m_screenHeight = 0;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_renderTargetView;

    D3D11_VIEWPORT m_viewport = {};
};
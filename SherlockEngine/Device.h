#pragma once
class Device
{
public:
    bool InitDevice(HWND hWnd);
    void ReleaseDevice();



private:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11RenderTargetView> renderTargetView;
    ComPtr<IDXGISwapChain> swapChain;
};


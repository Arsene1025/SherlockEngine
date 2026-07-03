#pragma once

class AppBase;
class Device
{
public:
    bool InitDevice(HWND hWnd, int width, int height, AppBase* app);
    void ReleaseDevice();

    void Clear();
    void Present();
    void Resize(int width, int height);

    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    IDXGISwapChain* GetSwapChain() const { return m_swapChain.Get(); }
    ID3D11DepthStencilView* GetDSView() const { return m_DSView.Get(); }
    void CreateConstBuffer(int size, ID3D11Buffer** ppCB);


    void CreateVertexBuffer(LPVOID pData, UINT size, UINT stride, ID3D11Buffer** ppVB);
    void CreateInputLayout(D3D11_INPUT_ELEMENT_DESC* desc, DWORD num, ID3DBlob* pVSCode, ID3D11InputLayout** ppLayout);
    void CreateRenderState();
    //
    AppBase* GetApp() const { return appBase; }

private:
    bool InitDirect3D();
    bool CreateRenderTargetView();
    void SetViewport();

public:
    UINT m_numQualityLevels = 0;

    //일단 App여기서 임시로 가지고 있음 -> 교수님 코드에서 Render가 Device나 Shader에 접근해야 하므로.
    AppBase* appBase;

private:
    HWND m_mainWindow = nullptr;
    int m_screenWidth = 0;
    int m_screenHeight = 0;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_renderTargetView;
    ComPtr<ID3D11DepthStencilView> m_DSView;

    D3D11_VIEWPORT m_viewport = {};
};
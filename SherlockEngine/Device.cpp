#include "pch.h"
#include "Device.h"
#include "AppBase.h"

bool Device::InitDevice(HWND hWnd, int width, int height)
{
    m_mainWindow = hWnd;
    m_screenWidth = width;
    m_screenHeight = height;

    if (!InitDirect3D()) return false;

    if (!CreateRenderTargetView()) return false;

    SetViewport();

    return true;
}

void Device::ReleaseDevice()
{
    if (m_context)
    {
        m_context->ClearState();
        m_context->Flush();
    }
    m_renderTargetView.Reset();
    m_swapChain.Reset();
    m_context.Reset();
    m_device.Reset();
}

void Device::Clear()
{
    if (!m_context || !m_renderTargetView)
        return;

    const float clearColor[4] =
    {
        0.1f, 0.1f, 0.3f, 1.0f
    };

    m_context->ClearRenderTargetView
    (
        m_renderTargetView.Get(),
        clearColor
    );
}

void Device::Present()
{
    if (!m_swapChain)
        return;

    //60프레임 제한
    //m_swapChain->Present(1, 0);

    //제한 없음
    m_swapChain->Present(0, 0);
}

void Device::Resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;

    if (!m_device || !m_context || !m_swapChain) return;

    m_screenWidth = width;
    m_screenHeight = height;

    // 기존 렌더 타깃 바인딩 해제
    ID3D11RenderTargetView* nullRTV = nullptr;
    m_context->OMSetRenderTargets(1, &nullRTV, nullptr);

    // 기존 RenderTargetView 해제
    m_renderTargetView.Reset();
    
    // SwapChain 백버퍼 크기 변경
    HRESULT hr = m_swapChain->ResizeBuffers
    (
        0,                          // 기존 BufferCount 유지
        m_screenWidth,
        m_screenHeight,
        DXGI_FORMAT_UNKNOWN,        // 기존 포맷 유지
        0
    );

    if (FAILED(hr))
    {
        std::cout << "ResizeBuffers() 실패." << std::endl;
        return;
    }

    // 변경된 BackBuffer로 RenderTargetView 다시 생성
    if (!CreateRenderTargetView())
    {
        std::cout << "Resize 후 RenderTargetView 재생성 실패." << std::endl;
        return;
    }
    // Viewport도 새 크기로 다시 설정
    SetViewport();
}

bool Device::InitDirect3D()
{
    const D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_HARDWARE;

    UINT createDeviceFlags = 0;

//#if defined(DEBUG) || defined(_DEBUG)
//    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
//#endif

    const D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_9_3
    };

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = m_screenWidth;
    sd.BufferDesc.Height = m_screenHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;

    sd.BufferCount = 2;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_mainWindow;
    sd.Windowed = TRUE;

    // 일단 MSAA 없이 시작하는 버전
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;

    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                        // 기본 그래픽 어댑터
        driverType,                     // 하드웨어 드라이버
        nullptr,                        // 소프트웨어 래스터라이저 사용 안 함
        createDeviceFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &sd,
        m_swapChain.GetAddressOf(),
        m_device.GetAddressOf(),
        &featureLevel,
        m_context.GetAddressOf()
    );

    if (FAILED(hr))
    {
        std::cout << "D3D11CreateDeviceAndSwapChain() 실패." << std::endl;
        return false;
    }

    if (featureLevel != D3D_FEATURE_LEVEL_11_0)
    {
        std::cout << "D3D Feature Level 11_0 지원 안 함." << std::endl;
        return false;
    }

    // 4X MSAA 지원 여부확인
    hr = m_device->CheckMultisampleQualityLevels
    (
        DXGI_FORMAT_R8G8B8A8_UNORM,
        4,
        &m_numQualityLevels
    );

    if (FAILED(hr) || m_numQualityLevels <= 0)
    {
        std::cout << "4X MSAA 지원 안 됨." << std::endl;
    }
    else
    {
        std::cout << "4X MSAA 지원됨. QualityLevels: " << m_numQualityLevels << std::endl;
    }
    return true;
}

bool Device::CreateRenderTargetView()
{
    ComPtr<ID3D11Texture2D> backBuffer;

    HRESULT hr = m_swapChain->GetBuffer
    (
        0,
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(backBuffer.GetAddressOf())
    );

    if (FAILED(hr))
    {
        std::cout << "SwapChain BackBuffer 가져오기 실패." << std::endl;
        return false;
    }

    hr = m_device->CreateRenderTargetView(
        backBuffer.Get(),
        nullptr,
        m_renderTargetView.GetAddressOf()
    );

    if (FAILED(hr))
    {
        std::cout << "CreateRenderTargetView() 실패." << std::endl;
        return false;
    }

    m_context->OMSetRenderTargets(
        1,
        m_renderTargetView.GetAddressOf(),
        nullptr
    );

    return true;
}

void Device::SetViewport()
{
    ZeroMemory(&m_viewport, sizeof(D3D11_VIEWPORT));
    m_viewport.TopLeftX = 0.0f;
    m_viewport.TopLeftY = 0.0f;
    m_viewport.Width = static_cast<float>(m_screenWidth);
    m_viewport.Height = static_cast<float>(m_screenHeight);
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &m_viewport);
}
#pragma once
#include "Graphics/D3D11/D3D11Common.h"
#include "Graphics/D3D11/PipelineStateCache.h"
#include "Graphics/Handle.h"
#include "Graphics/PipelineTypes.h"
#include <vector>

class Device
{
public:
    // 셰이더 풀의 항목. 입력 레이아웃 생성에 VS 바이트코드가 필요하므로 사본을 보관한다.
    struct ShaderEntry
    {
        ShaderStage stage = ShaderStage::Vertex;
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        std::vector<uint8_t> bytecode;
    };

    ~Device() { ReleaseDevice(); }

    bool InitDevice(HWND hWnd, int width, int height);
    void ReleaseDevice();   // 두 번 불러도 안전하다.

    // Debug Layer가 InfoQueue에 쌓아 둔 메시지를 Log로 옮긴다(Debug 빌드만).
    // 디버거 없이 실행해도 D3D 오류·경고·Live Object 보고가 콘솔에 보인다.
    void DumpDebugLayerMessages();

    void Clear();
    void Present();
    void Resize(int width, int height);

    // Present(1, 0) = 수직 동기화. 기본은 꺼짐(프레임 제한 없음).
    void SetVSync(bool enabled) { m_vsync = enabled; }
    bool IsVSync() const { return m_vsync; }

    // 셰이더. 바이트코드를 받아 D3D11 셰이더 객체를 만들고 핸들을 돌려준다.
    // 컴파일은 여기서 하지 않는다 (RHI는 바이트코드만 받는다).
    ShaderHandle CreateShader(ShaderStage stage, const void* bytecode, size_t size);
    const ShaderEntry* GetShader(ShaderHandle handle) const;   // Graphics/D3D11/ 안에서만 쓸 것

    // 파이프라인 상태. 같은 Desc는 같은 핸들을 돌려준다(캐시).
    PipelineHandle CreatePipeline(const PipelineStateDesc& desc);
    void BindPipeline(PipelineHandle handle);
    size_t GetPipelineCount() const { return m_pipelines.Count(); }

    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    IDXGISwapChain* GetSwapChain() const { return m_swapChain.Get(); }
    ID3D11DepthStencilView* GetDSView() const { return m_DSView.Get(); }

    //생성 함수. 실패하면 비어 있는(null) ComPtr을 돌려준다.
    // 3단계에서 BufferDesc → BufferHandle 방식으로 바뀐다.
    ComPtr<ID3D11Buffer> CreateConstBuffer(UINT size);
    ComPtr<ID3D11Buffer> CreateVertexBuffer(const void* pData, UINT size, UINT stride);
    ComPtr<ID3D11Buffer> CreateIndexBuffer(const void* pData, UINT size);

private:
    bool InitDirect3D();
    bool CreateRenderTargetView();
    bool CreateDepthStencilView();
    void BindRenderTargets();
    void SetViewport();

public:
    UINT m_numQualityLevels = 0;

private:
    HWND m_mainWindow = nullptr;
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    bool m_vsync = false;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_renderTargetView;
    ComPtr<ID3D11Texture2D> m_depthStencilBuffer;
    ComPtr<ID3D11DepthStencilView> m_DSView;

    D3D11_VIEWPORT m_viewport = {};

    // 풀과 캐시는 m_device보다 뒤에 선언한다. 멤버는 선언의 역순으로 파괴되므로
    // 이것들이 먼저 사라지고 나서 장치가 해제된다. 순서가 반대면 Debug Layer가
    // 종료 시 Live Object를 보고한다.
    std::vector<ShaderEntry> m_shaders;
    PipelineStateCache m_pipelines;
};

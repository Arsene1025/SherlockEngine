#pragma once
#include "Graphics/D3D11/D3D11Common.h"
#include "Graphics/D3D11/D3D11Resources.h"
#include "Graphics/D3D11/ResourcePool.h"
#include "Graphics/D3D11/PipelineStateCache.h"
#include "Graphics/Handle.h"
#include "Graphics/PipelineTypes.h"
#include "Graphics/ResourceDesc.h"
#include "Graphics/GraphicsConfig.h"

// D3D11 장치와 리소스 풀.
//
// 폴더 밖의 코드는 이 클래스의 "핸들 API"만 쓴다: Create*/Destroy*/UpdateBuffer/
// Bind*/DrawIndexed/BeginFrame/EndFrame. ID3D11* 타입을 돌려주는 Get* 접근자는
// Graphics/D3D11/ 안(PipelineState, ImGuiBackend)만 부른다.
//
// 7단계에서 이 공개 API가 그대로 RHI::Device 가상 인터페이스가 된다.
// Bind*/DrawIndexed는 6단계에서 CommandList로 옮겨 간다.
class Device
{
public:
    ~Device() { ReleaseDevice(); }

    bool InitDevice(HWND hWnd, int width, int height);
    void ReleaseDevice();   // 두 번 불러도 안전하다.
    bool IsInitialized() const { return m_device != nullptr; }

    // ---- 프레임 ----
    // 백버퍼·깊이 버퍼를 바인딩하고 클리어하고 뷰포트를 건다. 프레임 인덱스를 돌려준다.
    // Dynamic 버퍼는 이 인덱스의 복제본을 쓴다 (GraphicsConfig.h).
    uint32_t BeginFrame();
    void EndFrame();        // Present + Debug Layer 메시지 덤프 + 프레임 카운터 증가
    uint32_t GetFrameIndex() const { return m_frameIndex; }

    void Resize(int width, int height);
    void SetVSync(bool enabled) { m_vsync = enabled; }
    bool IsVSync() const { return m_vsync; }
    void SetClearColor(float r, float g, float b, float a);

    // ---- 리소스: Desc → 핸들 ----
    BufferHandle CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr);
    void DestroyBuffer(BufferHandle handle);
    // Dynamic: Map(WRITE_DISCARD) — 이 프레임 슬롯의 내용을 통째로 교체한다. size ≤ desc.size.
    // Default: UpdateSubresource — 전체 크기(size == desc.size)만 허용한다.
    void UpdateBuffer(BufferHandle handle, const void* data, uint32_t size);

    TextureHandle CreateTexture(const TextureDesc& desc, const void* initialData = nullptr);
    void DestroyTexture(TextureHandle handle);
    // Resize마다 새로 만들어진다. 프레임을 넘겨 보관하지 말 것.
    TextureHandle GetBackBuffer() const { return m_backBuffer; }
    TextureHandle GetDepthBuffer() const { return m_depthBuffer; }

    ShaderHandle CreateShader(const ShaderDesc& desc);
    void DestroyShader(ShaderHandle handle);

    PipelineHandle CreatePipeline(const PipelineStateDesc& desc);   // 같은 Desc는 같은 핸들(캐시)
    size_t GetPipelineCount() const { return m_pipelines.Count(); }

    // ---- 즉시 바인딩·드로우 (6단계에서 CommandList로 이동) ----
    void BindPipeline(PipelineHandle handle);
    void BindVertexBuffer(BufferHandle handle, uint32_t slot = 0, uint32_t offset = 0);   // stride는 desc.stride
    void BindIndexBuffer(BufferHandle handle, Format indexFormat = Format::R32_UINT);
    void BindConstantBuffer(uint32_t slot, BufferHandle handle, uint8_t stageMask);       // ShaderStageMask_*
    void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0, int32_t baseVertex = 0);

    // Debug Layer가 InfoQueue에 쌓아 둔 메시지를 Log로 옮긴다(Debug 빌드만).
    void DumpDebugLayerMessages();

    // ---- Graphics/D3D11/ 안에서만 쓰는 접근자 ----
    ID3D11Device* GetDevice() const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }
    const D3D11Shader* GetShader(ShaderHandle handle) const { return m_shaders.Get(handle); }

private:
    bool InitDirect3D();
    bool CreateBackBufferTexture();   // 스왑체인의 버퍼 0을 Texture로 등록
    bool CreateDepthTexture();
    void UpdateViewport();
    static void SetDebugName(ID3D11DeviceChild* object, const std::string& name, uint32_t index);

public:
    UINT m_numQualityLevels = 0;

private:
    HWND m_mainWindow = nullptr;
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    bool m_vsync = false;
    float m_clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };

    uint64_t m_frameCounter = 0;
    uint32_t m_frameIndex = 0;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    D3D11_VIEWPORT m_viewport = {};

    // 풀과 캐시는 m_device보다 뒤에 선언한다. 멤버는 선언의 역순으로 파괴되므로
    // 이것들이 먼저 사라지고 나서 장치가 해제된다. 순서가 반대면 Debug Layer가
    // 종료 시 Live Object를 보고한다. ReleaseDevice도 같은 순서로 비운다.
    ResourcePool<D3D11Buffer, BufferHandle> m_buffers;
    ResourcePool<D3D11Texture, TextureHandle> m_textures;
    ResourcePool<D3D11Shader, ShaderHandle> m_shaders;
    PipelineStateCache m_pipelines;

    TextureHandle m_backBuffer;
    TextureHandle m_depthBuffer;
};

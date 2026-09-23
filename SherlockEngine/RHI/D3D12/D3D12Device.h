#pragma once
#include "RHI/D3D12/D3D12Common.h"
#include "RHI/D3D12/D3D12Resources.h"
#include "RHI/D3D12/D3D12DescriptorHeap.h"
#include "RHI/D3D12/D3D12PipelineState.h"
#include "RHI/D3D12/D3D12CommandList.h"
#include "RHI/ResourcePool.h"
#include "RHI/Device.h"
#include "RHI/GraphicsConfig.h"
#include <functional>
#include <memory>
#include <unordered_map>

// RHI::Device 의 D3D12 구현 (8단계, 최소 구성).
//
//   장치·직접 커맨드 큐·flip 스왑체인(백버퍼 3장), 프레임별 커맨드 할당자 + 단일 커맨드 리스트,
//   펜스로 프레임 N−kFrameCount 완료 대기, 디스크립터 힙(RTV·DSV·스테이징 SRV/샘플러 CPU 전용,
//   셰이더 가시 CBV/SRV/UAV·샘플러), 프레임별 업로드 링(Dynamic 버퍼), 동기 업로드(초기 데이터),
//   지연 해제(GPU 가 아직 쓰는 리소스·디스크립터는 그 프레임의 펜스 뒤에 놓는다).
//
// D3D11 에서 no-op 이던 것들이 여기서 진짜가 된다: Barrier → ResourceBarrier, BindingLayout → 루트 시그니처,
// ResourceSet → 디스크립터 테이블, kFrameCount → 펜스와 링의 기준, RTV/DSV 포맷 → PSO 필수 항목.
// 셰이더는 fxc 가 만든 DXBC(SM 5.0) 를 그대로 쓴다. D3D12 는 DXBC 를 받는다 — 두 백엔드가 같은 .cso 를
// 쓰므로 픽셀 비교가 의미 있다. DXIL(dxc, SM 6.x) 은 SM 6 기능이 필요할 때 넣는다.
class D3D12Device final : public RHI::Device
{
public:
    static constexpr uint32_t kBackBufferCount = 3;
    static constexpr uint64_t kUploadRingSize = 8ull * 1024 * 1024;   // 프레임 슬롯마다

    ~D3D12Device() override { ReleaseDevice(); }

    bool Init(const RHI::DeviceDesc& desc);
    void ReleaseDevice();

    // ---- RHI::Device ----
    RHI::Backend GetBackend() const override { return RHI::Backend::D3D12; }
    const char* GetBackendName() const override { return "D3D12"; }
    bool IsInitialized() const override { return m_device != nullptr; }

    uint32_t BeginFrame() override;
    void EndFrame() override;
    uint32_t GetFrameIndex() const override { return m_frameIndex; }
    RHI::CommandList& GetCommandList() override { return m_commandList; }
    bool GetTimestampResults(uint64_t* ticks, uint32_t count, uint64_t& frequency, uint64_t& frameNumber) override;
    void RequestBackBufferReadback() override { m_readbackRequested = true; }
    bool TakeReadbackResult(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height) override;
    void WriteTimestamp(uint32_t slot);   // D3D12CommandList 가 위임한다

    void Resize(int width, int height) override;
    void SetVSync(bool enabled) override { m_vsync = enabled; }
    bool IsVSync() const override { return m_vsync; }
    bool IsTearingSupported() const override { return m_tearingSupported; }

    BufferHandle CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) override;
    void DestroyBuffer(BufferHandle handle) override;
    void UpdateBuffer(BufferHandle handle, const void* data, uint32_t size) override;

    TextureHandle CreateTexture(const TextureDesc& desc, const TextureSubresource* subresources = nullptr, uint32_t subresourceCount = 0) override;
    void DestroyTexture(TextureHandle handle) override;
    TextureHandle GetBackBuffer() const override { return m_backBuffers[m_backBufferIndex]; }
    TextureHandle GetDepthBuffer() const override { return m_depthBuffer; }

    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void DestroySampler(SamplerHandle handle) override;

    ShaderHandle CreateShader(const ShaderDesc& desc) override;
    void DestroyShader(ShaderHandle handle) override;

    PipelineHandle CreatePipeline(const PipelineStateDesc& desc) override;
    size_t GetPipelineCount() const override { return m_pipelines.Count(); }
    void InvalidatePipelines() override;

    BindingLayoutHandle CreateBindingLayout(const BindingLayoutDesc& desc) override;
    void DestroyBindingLayout(BindingLayoutHandle handle) override;
    ResourceSetHandle CreateResourceSet(const ResourceSetDesc& desc) override;
    void DestroyResourceSet(ResourceSetHandle handle) override;

    bool InitImGui() override;
    void NewFrameImGui() override;
    void RenderImGui() override;
    void ShutdownImGui() override;
    uint64_t GetImGuiTextureId(TextureHandle texture) override;

    void DumpDebugLayerMessages();

    // ---- RHI/D3D12/ 안에서만 쓰는 접근자 ----
    ID3D12Device* GetDevice() const { return m_device.Get(); }
    uint64_t GetFrameNumber() const { return m_frameCounter; }
    const D3D12Shader* GetShader(ShaderHandle handle) const { return m_shaders.Get(handle); }
    const D3D12Buffer* GetBuffer(BufferHandle handle) const { return m_buffers.Get(handle); }
    D3D12Texture* GetTexture(TextureHandle handle) { return m_textures.Get(handle); }
    const D3D12Sampler* GetSampler(SamplerHandle handle) const { return m_samplers.Get(handle); }
    const D3D12BindingLayout* GetBindingLayout(BindingLayoutHandle handle) const { return m_bindingLayouts.Get(handle); }
    const D3D12ResourceSet* GetResourceSet(ResourceSetHandle handle) const { return m_resourceSets.Get(handle); }
    const D3D12PipelineState* GetPipeline(PipelineHandle handle) const { return m_pipelines.Get(handle); }
    D3D12DescriptorHeap& GetSrvHeap() { return m_srvHeap; }
    D3D12DescriptorHeap& GetSamplerHeap() { return m_samplerHeap; }

    // 뷰는 처음 요청될 때 만든다.
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(D3D12Texture& texture, bool srgbView);
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(D3D12Texture& texture);
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(D3D12Texture& texture);   // 스테이징 힙

    // 레이아웃 조합 → 루트 시그니처 (캐시)
    std::shared_ptr<D3D12RootLayout> GetOrCreateRootLayout(const PipelineStateDesc& desc);

    // GPU 가 아직 쓰고 있을 수 있는 객체·디스크립터를 이번 프레임의 펜스 뒤에 놓는다.
    void DeferRelease(ComPtr<ID3D12Object> object);
    void DeferFree(D3D12DescriptorHeap* heap, uint32_t start, uint32_t count);

private:
    bool InitDevice(bool debugLayer);
    bool InitSwapChain();
    bool InitFrameResources();
    bool CreateBackBufferTextures();
    bool CreateDepthTexture();
    void ReleaseTextureNow(TextureHandle handle);   // GPU 유휴 상태에서만 (Resize)
    void WaitForGpu();
    void ReleaseGarbage(uint32_t slot);
    // 동기 업로드: 별도 커맨드 리스트에 기록·실행·완료 대기. 초기 데이터 전용 (로드 시점).
    bool ExecuteUploadSync(const std::function<void(ID3D12GraphicsCommandList*)>& record);
    // 프레임 업로드 링에서 조각 할당 (256B 정렬). 실패하면 nullptr.
    uint8_t* AllocateUpload(uint32_t size, D3D12_GPU_VIRTUAL_ADDRESS& outAddress);
    static void SetDebugName(ID3D12Object* object, const std::string& name);

    static void ImGuiSrvAlloc(struct ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu);
    static void ImGuiSrvFree(struct ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);

private:
    HWND m_mainWindow = nullptr;
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    bool m_vsync = false;
    bool m_tearingSupported = false;
    bool m_debugLayer = false;
    bool m_imguiInitialized = false;
    UINT m_swapChainFlags = 0;

    uint64_t m_frameCounter = 0;
    uint32_t m_frameIndex = 0;
    uint32_t m_backBufferIndex = 0;

    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12InfoQueue> m_infoQueue;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<IDXGISwapChain3> m_swapChain;

    // 프레임 자원 (kFrameCount 개가 동시에 진행 중일 수 있다)
    ComPtr<ID3D12CommandAllocator> m_allocators[kFrameCount];
    ComPtr<ID3D12GraphicsCommandList> m_list;
    ComPtr<ID3D12Fence> m_fence;
    uint64_t m_fenceValues[kFrameCount] = {};
    uint64_t m_nextFenceValue = 1;
    HANDLE m_fenceEvent = nullptr;

    // 동기 업로드
    ComPtr<ID3D12CommandAllocator> m_uploadAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_uploadList;

    // 업로드 링 (Dynamic 버퍼)
    struct UploadRing
    {
        ComPtr<ID3D12Resource> resource;
        uint8_t* mapped = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS base = 0;
        uint64_t offset = 0;
        uint64_t peak = 0;
    };
    UploadRing m_rings[kFrameCount];

    // 디스크립터 힙
    D3D12DescriptorHeap m_rtvHeap;          // CPU
    D3D12DescriptorHeap m_dsvHeap;          // CPU
    D3D12DescriptorHeap m_srvStaging;       // CPU. 텍스처마다 SRV 하나
    D3D12DescriptorHeap m_samplerStaging;   // CPU
    D3D12DescriptorHeap m_srvHeap;          // 셰이더 가시. ResourceSet 테이블 + ImGui
    D3D12DescriptorHeap m_samplerHeap;      // 셰이더 가시

    // 지연 해제
    struct DescriptorFree { D3D12DescriptorHeap* heap; uint32_t start; uint32_t count; };
    struct Garbage
    {
        std::vector<ComPtr<ID3D12Object>> objects;
        std::vector<DescriptorFree> descriptors;
    };
    Garbage m_garbage[kFrameCount];

    // 풀과 캐시. 장치보다 먼저 파괴되어야 한다 (선언 역순).
    ResourcePool<D3D12Buffer, BufferHandle> m_buffers;
    ResourcePool<D3D12Texture, TextureHandle> m_textures;
    ResourcePool<D3D12Sampler, SamplerHandle> m_samplers;
    ResourcePool<D3D12Shader, ShaderHandle> m_shaders;
    ResourcePool<D3D12BindingLayout, BindingLayoutHandle> m_bindingLayouts;
    ResourcePool<D3D12ResourceSet, ResourceSetHandle> m_resourceSets;
    std::unordered_map<uint64_t, std::shared_ptr<D3D12RootLayout>> m_rootLayouts;
    D3D12PipelineStateCache m_pipelines;
    D3D12CommandList m_commandList;

    TextureHandle m_backBuffers[kBackBufferCount];
    TextureHandle m_depthBuffer;

    // 10단계: 타임스탬프. 쿼리 힙은 프레임 슬롯마다 kMaxTimestamps 개, 리드백 버퍼는 같은 배치의 uint64.
    // EndFrame 이 이번 슬롯의 기록된 범위를 Resolve 하고, BeginFrame 이 (펜스를 지난) 재사용 슬롯의 결과를 읽는다.
    ComPtr<ID3D12QueryHeap> m_timestampHeap;
    ComPtr<ID3D12Resource> m_timestampReadback;
    uint32_t m_timestampWritten[kFrameCount] = {};   // 슬롯마다 기록된 최대 인덱스 + 1
    uint64_t m_timestampFrame[kFrameCount] = {};
    uint64_t m_lastTicks[kMaxTimestamps] = {};
    uint64_t m_lastFrequency = 0;
    uint64_t m_lastFrameNumber = 0;
    bool m_hasTimestampResults = false;
    bool CreateTimestampResources();
    // 11단계: 백버퍼 리드백. EndFrame 이 Close 전에 복사를 기록하고, 실행 뒤 GPU 를 기다려 읽는다.
    void RecordBackBufferReadback();
    void FinishBackBufferReadback();
    bool m_readbackRequested = false;
    bool m_readbackPending = false;
    bool m_readbackReady = false;
    ComPtr<ID3D12Resource> m_readbackBuffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_readbackFootprint = {};
    std::vector<uint8_t> m_readbackPixels;
    uint32_t m_readbackWidth = 0;
    uint32_t m_readbackHeight = 0;
    void ReadTimestamps(uint32_t slot);
};

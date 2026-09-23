#include "pch.h"
#include "RHI/D3D12/D3D12Device.h"
#include "RHI/D3D12/D3D12Convert.h"
#include "Core/Log.h"
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <algorithm>
#include <cstring>

namespace
{
    void ErrorOnce(bool& flag, const char* message)
    {
        if (flag) return;
        flag = true;
        Log::Error("%s", message);
    }

    D3D12_SHADER_VISIBILITY VisibilityOf(uint8_t stageMask)
    {
        if (stageMask == ShaderStageMask_Vertex) return D3D12_SHADER_VISIBILITY_VERTEX;
        if (stageMask == ShaderStageMask_Pixel)  return D3D12_SHADER_VISIBILITY_PIXEL;
        return D3D12_SHADER_VISIBILITY_ALL;
    }

    std::wstring ToWide(const std::string& utf8)
    {
        if (utf8.empty()) return L"";
        const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
        std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
        if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
        return wide;
    }
}

// ------------------------------------------------------------------ 수명

bool D3D12Device::Init(const RHI::DeviceDesc& desc)
{
    m_mainWindow = static_cast<HWND>(desc.windowHandle);
    m_screenWidth = desc.width;
    m_screenHeight = desc.height;
    m_debugLayer = desc.enableDebugLayer;

    if (!InitDevice(desc.enableDebugLayer)) return false;
    if (!InitSwapChain()) return false;
    if (!InitFrameResources()) return false;
    m_commandList.Init(this);
    if (!CreateTimestampResources()) return false;   // 10단계
    if (!CreateBackBufferTextures()) return false;
    if (!CreateDepthTexture()) return false;
    return true;
}

void D3D12Device::SetDebugName(ID3D12Object* object, const std::string& name)
{
#if defined(_DEBUG)
    if (object == nullptr || name.empty()) return;
    object->SetName(ToWide(name).c_str());
#else
    (void)object; (void)name;
#endif
}

bool D3D12Device::InitDevice(bool debugLayer)
{
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    if (debugLayer)
    {
        // Debug Layer + GPU 기반 검증(GBV). GBV 는 배리어 누락·디스크립터 오용을 셰이더 실행 시점에 잡는다.
        // 프레임이 수십 배 느려지지만 8단계의 목적이 검증이므로 Debug 에서는 항상 켠다.
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()))))
        {
            debug->EnableDebugLayer();
            ComPtr<ID3D12Debug1> debug1;
            if (SUCCEEDED(debug.As(&debug1)))
            {
                debug1->SetEnableGPUBasedValidation(TRUE);
                debug1->SetEnableSynchronizedCommandQueueValidation(TRUE);
            }
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#else
    (void)debugLayer;
#endif

    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(m_factory.GetAddressOf()));
    if (FAILED(hr))
    {
        Log::Error("CreateDXGIFactory2 실패. %s", Log::HrToString(hr).c_str());
        return false;
    }

    // 첫 하드웨어 어댑터 (WARP 제외) 중 FL 11_0 을 지원하는 것.
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; m_factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 ad = {};
        adapter->GetDesc1(&ad);
        if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_device.GetAddressOf()))))
        {
            Log::Info("D3D12 어댑터: %s", Log::ToUtf8(ad.Description).c_str());
            break;
        }
    }
    if (!m_device)
    {
        Log::Error("D3D12CreateDevice 실패: FL 11_0 하드웨어 어댑터가 없음.");
        return false;
    }
    SetDebugName(m_device.Get(), "D3D12Device");

#if defined(_DEBUG)
    if (SUCCEEDED(m_device.As(&m_infoQueue)))
    {
        // 클리어 값 불일치 경고: 스왑체인 백버퍼에는 최적화 클리어 값을 줄 수 없으므로 항상 난다. 성능 힌트일 뿐이라 거른다.
        D3D12_MESSAGE_ID denyIds[] =
        {
            D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
            D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
        };
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumIDs = ARRAYSIZE(denyIds);
        filter.DenyList.pIDList = denyIds;
        m_infoQueue->AddStorageFilterEntries(&filter);
    }
#endif

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    qd.NodeMask = 0;
    hr = m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(m_queue.GetAddressOf()));
    if (FAILED(hr))
    {
        Log::Error("CreateCommandQueue 실패. %s", Log::HrToString(hr).c_str());
        return false;
    }
    SetDebugName(m_queue.Get(), "DirectQueue");

    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(m_factory.As(&factory5)))
    {
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
        {
            m_tearingSupported = (allowTearing != FALSE);
        }
    }
    m_swapChainFlags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    return true;
}

bool D3D12Device::InitSwapChain()
{
    // D3D11 과 같은 flip 모델. 차이: pDevice 자리에 커맨드 큐가 들어가고, 백버퍼가 3장이다.
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = m_screenWidth;
    sd.Height = m_screenHeight;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = kBackBufferCount;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sd.Flags = m_swapChainFlags;

    ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT hr = m_factory->CreateSwapChainForHwnd(m_queue.Get(), m_mainWindow, &sd, nullptr, nullptr, swapChain1.GetAddressOf());
    if (FAILED(hr))
    {
        Log::Error("CreateSwapChainForHwnd() 실패. %s", Log::HrToString(hr).c_str());
        return false;
    }
    if (FAILED(swapChain1.As(&m_swapChain)))
    {
        Log::Error("IDXGISwapChain3 얻기 실패.");
        return false;
    }
    m_factory->MakeWindowAssociation(m_mainWindow, DXGI_MWA_NO_ALT_ENTER);
    Log::Info("스왑체인: FLIP_DISCARD, 버퍼 %u, tearing %s", kBackBufferCount, m_tearingSupported ? "지원" : "미지원");
    return true;
}

bool D3D12Device::InitFrameResources()
{
    HRESULT hr;
    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_allocators[i].GetAddressOf()));
        if (FAILED(hr)) { Log::Error("CreateCommandAllocator 실패. %s", Log::HrToString(hr).c_str()); return false; }
        SetDebugName(m_allocators[i].Get(), "FrameAllocator[" + std::to_string(i) + "]");
    }
    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].Get(), nullptr, IID_PPV_ARGS(m_list.GetAddressOf()));
    if (FAILED(hr)) { Log::Error("CreateCommandList 실패. %s", Log::HrToString(hr).c_str()); return false; }
    m_list->Close();   // BeginFrame 이 Reset 으로 연다
    SetDebugName(m_list.Get(), "FrameCommandList");

    hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.GetAddressOf()));
    if (FAILED(hr)) { Log::Error("CreateFence 실패. %s", Log::HrToString(hr).c_str()); return false; }
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr) { Log::Error("CreateEvent 실패."); return false; }

    hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_uploadAllocator.GetAddressOf()));
    if (FAILED(hr)) return false;
    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_uploadAllocator.Get(), nullptr, IID_PPV_ARGS(m_uploadList.GetAddressOf()));
    if (FAILED(hr)) return false;
    m_uploadList->Close();
    SetDebugName(m_uploadList.Get(), "UploadCommandList");

    // 업로드 링: 프레임 슬롯마다 UPLOAD 힙 버퍼 하나, 영구 매핑. Dynamic 버퍼의 UpdateBuffer 가 여기서 조각을 받는다.
    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        const D3D12_HEAP_PROPERTIES heap = D3D12Convert::HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC rd = D3D12Convert::BufferDesc(kUploadRingSize);
        hr = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(m_rings[i].resource.GetAddressOf()));
        if (FAILED(hr)) { Log::Error("업로드 링 생성 실패. %s", Log::HrToString(hr).c_str()); return false; }
        SetDebugName(m_rings[i].resource.Get(), "UploadRing[" + std::to_string(i) + "]");
        const D3D12_RANGE noRead = { 0, 0 };
        void* mapped = nullptr;
        if (FAILED(m_rings[i].resource->Map(0, &noRead, &mapped))) return false;
        m_rings[i].mapped = static_cast<uint8_t*>(mapped);
        m_rings[i].base = m_rings[i].resource->GetGPUVirtualAddress();
        m_rings[i].offset = 0;
    }

    // 디스크립터 힙
    if (!m_rtvHeap.Init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 64, false, L"RTVHeap")) return false;
    if (!m_dsvHeap.Init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 16, false, L"DSVHeap")) return false;
    if (!m_srvStaging.Init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1024, false, L"SRVStaging")) return false;
    if (!m_samplerStaging.Init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 64, false, L"SamplerStaging")) return false;
    if (!m_srvHeap.Init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096, true, L"SRVHeap(GPU)")) return false;
    if (!m_samplerHeap.Init(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 256, true, L"SamplerHeap(GPU)")) return false;

    Log::Info("D3D12 프레임 자원: 프레임 %u개 동시 진행, 업로드 링 %llu MB/프레임, 셰이더 가시 힙 SRV %u / 샘플러 %u",
        kFrameCount, kUploadRingSize / (1024 * 1024), m_srvHeap.GetCapacity(), m_samplerHeap.GetCapacity());
    return true;
}

void D3D12Device::WaitForGpu()
{
    if (!m_queue || !m_fence) return;
    const uint64_t value = m_nextFenceValue++;
    m_queue->Signal(m_fence.Get(), value);
    if (m_fence->GetCompletedValue() < value)
    {
        m_fence->SetEventOnCompletion(value, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void D3D12Device::ReleaseGarbage(uint32_t slot)
{
    Garbage& g = m_garbage[slot];
    for (const DescriptorFree& f : g.descriptors) f.heap->Free(f.start, f.count);
    g.descriptors.clear();
    g.objects.clear();   // ComPtr 해제
}

void D3D12Device::DeferRelease(ComPtr<ID3D12Object> object)
{
    if (object) m_garbage[m_frameIndex].objects.push_back(std::move(object));
}

void D3D12Device::DeferFree(D3D12DescriptorHeap* heap, uint32_t start, uint32_t count)
{
    if (heap && start != D3D12DescriptorHeap::kInvalid && count > 0)
    {
        m_garbage[m_frameIndex].descriptors.push_back(DescriptorFree{ heap, start, count });
    }
}

void D3D12Device::ReleaseDevice()
{
    if (!m_device) return;
    WaitForGpu();
    ShutdownImGui();

    // GPU 가 유휴이므로 전부 즉시 놓는다. 순서: 풀·캐시 → 지연 목록 → 힙 → 프레임 자원 → 스왑체인 → 큐 → 장치.
    m_pipelines.Clear(*this);
    m_rootLayouts.clear();
    m_resourceSets.Clear();
    m_bindingLayouts.Clear();
    m_shaders.Clear();
    m_samplers.Clear();
    m_textures.Clear();
    m_buffers.Clear();
    for (uint32_t i = 0; i < kFrameCount; ++i) ReleaseGarbage(i);
    for (TextureHandle& h : m_backBuffers) h = TextureHandle{};
    m_depthBuffer = TextureHandle{};

    m_timestampHeap.Reset();
    m_timestampReadback.Reset();
    m_readbackBuffer.Reset();
    m_rtvHeap.Release();
    m_dsvHeap.Release();
    m_srvStaging.Release();
    m_samplerStaging.Release();
    m_srvHeap.Release();
    m_samplerHeap.Release();

    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        if (m_rings[i].resource && m_rings[i].mapped) m_rings[i].resource->Unmap(0, nullptr);
        m_rings[i].resource.Reset();
        m_rings[i].mapped = nullptr;
        m_allocators[i].Reset();
    }
    m_uploadList.Reset();
    m_uploadAllocator.Reset();
    m_list.Reset();
    m_fence.Reset();
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
    m_swapChain.Reset();
    m_queue.Reset();

#if defined(_DEBUG)
    {
        ComPtr<ID3D12DebugDevice> debugDevice;
        if (SUCCEEDED(m_device.As(&debugDevice)))
        {
            debugDevice->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
        }
        DumpDebugLayerMessages();
    }
#endif
    m_infoQueue.Reset();
    m_device.Reset();
    m_factory.Reset();
}

void D3D12Device::DumpDebugLayerMessages()
{
#if defined(_DEBUG)
    if (!m_infoQueue) return;
    const UINT64 count = m_infoQueue->GetNumStoredMessages();
    std::vector<char> buffer;
    for (UINT64 i = 0; i < count; ++i)
    {
        SIZE_T length = 0;
        if (FAILED(m_infoQueue->GetMessage(i, nullptr, &length)) || length == 0) continue;
        buffer.resize(length);
        D3D12_MESSAGE* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
        if (FAILED(m_infoQueue->GetMessage(i, message, &length))) continue;
        switch (message->Severity)
        {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        case D3D12_MESSAGE_SEVERITY_ERROR:
            Log::Error("D3D12: %.*s", static_cast<int>(message->DescriptionByteLength), message->pDescription);
            break;
        case D3D12_MESSAGE_SEVERITY_WARNING:
            Log::Warn("D3D12: %.*s", static_cast<int>(message->DescriptionByteLength), message->pDescription);
            break;
        default:
            Log::Info("D3D12: %.*s", static_cast<int>(message->DescriptionByteLength), message->pDescription);
            break;
        }
    }
    m_infoQueue->ClearStoredMessages();
#endif
}

// ------------------------------------------------------------------ 동기 업로드 / 링

bool D3D12Device::ExecuteUploadSync(const std::function<void(ID3D12GraphicsCommandList*)>& record)
{
    if (FAILED(m_uploadAllocator->Reset())) return false;
    if (FAILED(m_uploadList->Reset(m_uploadAllocator.Get(), nullptr))) return false;
    record(m_uploadList.Get());
    if (FAILED(m_uploadList->Close())) return false;
    ID3D12CommandList* lists[] = { m_uploadList.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    // 큐는 순서대로 실행하므로 이 펜스를 기다리면 업로드가 끝난 것이다. 로드 시점에만 쓰는 경로라 동기여도 된다.
    WaitForGpu();
    return true;
}

uint8_t* D3D12Device::AllocateUpload(uint32_t size, D3D12_GPU_VIRTUAL_ADDRESS& outAddress)
{
    static bool warnedFull = false;
    UploadRing& ring = m_rings[m_frameIndex];
    const uint64_t aligned = (ring.offset + 255) & ~255ull;   // 루트 CBV 는 256 바이트 정렬
    if (aligned + size > kUploadRingSize)
    {
        ErrorOnce(warnedFull, "UpdateBuffer : 프레임 업로드 링이 가득 참. kUploadRingSize 를 키울 것.");
        return nullptr;
    }
    outAddress = ring.base + aligned;
    ring.offset = aligned + size;
    if (ring.offset > ring.peak) ring.peak = ring.offset;
    return ring.mapped + aligned;
}

// ------------------------------------------------------------------ 프레임

uint32_t D3D12Device::BeginFrame()
{
    m_frameIndex = static_cast<uint32_t>(m_frameCounter % kFrameCount);

    // 이 슬롯을 마지막으로 쓴 프레임(N − kFrameCount)이 GPU 에서 끝났는지. D3D11 이 드라이버 안에서 하던 대기다.
    if (m_fenceValues[m_frameIndex] != 0 && m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    ReleaseGarbage(m_frameIndex);      // 그 프레임이 쓰던 리소스·디스크립터를 이제 놓아도 된다
    ReadTimestamps(m_frameIndex);      // 10단계: 그 프레임(N − kFrameCount)의 타임스탬프는 이제 리드백에 있다
    m_rings[m_frameIndex].offset = 0;  // 그 프레임의 업로드 링도 재사용

    m_allocators[m_frameIndex]->Reset();
    m_list->Reset(m_allocators[m_frameIndex].Get(), nullptr);
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get(), m_samplerHeap.Get() };
    m_list->SetDescriptorHeaps(2, heaps);

    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    m_commandList.ResetStats();
    m_commandList.BeginFrame(m_list.Get());
    return m_frameIndex;
}

void D3D12Device::EndFrame()
{
    if (m_commandList.IsInRenderPass())
    {
        static bool warned = false;
        ErrorOnce(warned, "EndFrame : 렌더 패스가 열린 채 Present. EndRenderPass 를 빠뜨렸다.");
        m_commandList.EndRenderPass();
    }
    // 10단계: 이번 슬롯에 기록된 타임스탬프를 리드백 버퍼로. 기록 안 한 쿼리는 Resolve 하지 않는다.
    if (m_timestampHeap && m_timestampWritten[m_frameIndex] > 0)
    {
        m_list->ResolveQueryData(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_frameIndex * kMaxTimestamps,
            m_timestampWritten[m_frameIndex], m_timestampReadback.Get(), static_cast<UINT64>(m_frameIndex) * kMaxTimestamps * sizeof(uint64_t));
    }
    m_timestampFrame[m_frameIndex] = m_frameCounter;
    if (m_readbackRequested) RecordBackBufferReadback();   // 11단계
    m_list->Close();
    ID3D12CommandList* lists[] = { m_list.Get() };
    m_queue->ExecuteCommandLists(1, lists);

    const UINT flags = (!m_vsync && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    const HRESULT hr = m_swapChain->Present(m_vsync ? 1 : 0, flags);
    if (FAILED(hr))
    {
        static bool warned = false;
        if (!warned) { warned = true; Log::Error("Present 실패. %s (DeviceRemoved: %s)", Log::HrToString(hr).c_str(), Log::HrToString(m_device->GetDeviceRemovedReason()).c_str()); }
    }

    // 이 프레임의 끝을 펜스로 표시한다. 같은 슬롯을 다시 쓰는 BeginFrame 이 이 값을 기다린다.
    m_queue->Signal(m_fence.Get(), m_nextFenceValue);
    m_fenceValues[m_frameIndex] = m_nextFenceValue++;
    if (m_readbackPending) FinishBackBufferReadback();   // 11단계: GPU 를 기다려 픽셀을 읽는다

    DumpDebugLayerMessages();
    ++m_frameCounter;
}

void D3D12Device::Resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    if (!m_device || !m_swapChain) return;
    m_screenWidth = width;
    m_screenHeight = height;

    // 백버퍼 참조가 GPU 에도 CPU 에도 남아 있으면 안 된다. GPU 를 비우고 즉시 놓는다.
    WaitForGpu();
    for (uint32_t i = 0; i < kFrameCount; ++i) ReleaseGarbage(i);
    for (TextureHandle& h : m_backBuffers) { ReleaseTextureNow(h); h = TextureHandle{}; }
    ReleaseTextureNow(m_depthBuffer);
    m_depthBuffer = TextureHandle{};

    const HRESULT hr = m_swapChain->ResizeBuffers(kBackBufferCount, m_screenWidth, m_screenHeight, DXGI_FORMAT_UNKNOWN, m_swapChainFlags);
    if (FAILED(hr))
    {
        Log::Error("ResizeBuffers() 실패. %s", Log::HrToString(hr).c_str());
        return;
    }
    if (!CreateBackBufferTextures() || !CreateDepthTexture())
    {
        Log::Error("Resize 후 백버퍼·깊이 버퍼 재생성 실패.");
    }
}

// ------------------------------------------------------------------ 버퍼

BufferHandle D3D12Device::CreateBuffer(const BufferDesc& descIn, const void* initialData)
{
    if (!m_device)
    {
        Log::Error("CreateBuffer : 장치가 없음.");
        return BufferHandle{};
    }
    if (descIn.size == 0)
    {
        Log::Error("CreateBuffer : 크기가 0 (%s).", descIn.debugName ? descIn.debugName : "");
        return BufferHandle{};
    }

    D3D12Buffer buffer;
    buffer.desc = descIn;
    buffer.desc.debugName = nullptr;
    buffer.name = descIn.debugName ? descIn.debugName : "";
    if (buffer.desc.bindFlags & BufferBind_Constant)
    {
        buffer.desc.size = (buffer.desc.size + 255) & ~255u;   // 루트 CBV: 256 바이트 정렬
    }

    if (buffer.desc.usage == BufferUsage::Dynamic)
    {
        // 리소스 없음. UpdateBuffer 가 프레임 링에서 조각을 준다 (D3D11 의 WRITE_DISCARD 이름 바꾸기와 같은 일).
        return m_buffers.Add(std::move(buffer));
    }

    const bool staging = (buffer.desc.usage == BufferUsage::Staging);
    const D3D12_HEAP_PROPERTIES heap = D3D12Convert::HeapProperties(staging ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_RESOURCE_DESC rd = D3D12Convert::BufferDesc(buffer.desc.size);
    const D3D12_RESOURCE_STATES initial = staging ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON;
    HRESULT hr = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, initial, nullptr, IID_PPV_ARGS(buffer.resource.GetAddressOf()));
    if (FAILED(hr))
    {
        Log::Error("CreateBuffer 실패 (%s, %u바이트). %s", buffer.name.c_str(), buffer.desc.size, Log::HrToString(hr).c_str());
        return BufferHandle{};
    }
    SetDebugName(buffer.resource.Get(), buffer.name);
    buffer.gpuAddress = buffer.resource->GetGPUVirtualAddress();

    if (initialData != nullptr && !staging)
    {
        // 업로드 힙 스테이징 → CopyBufferRegion. 버퍼는 COMMON 에서 COPY_DEST 로 암묵 승격되고
        // 실행이 끝나면 COMMON 으로 돌아오므로(decay) 배리어가 필요 없다.
        ComPtr<ID3D12Resource> upload;
        const D3D12_HEAP_PROPERTIES uploadHeap = D3D12Convert::HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
        hr = m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload.GetAddressOf()));
        if (FAILED(hr))
        {
            Log::Error("CreateBuffer : 업로드 버퍼 생성 실패 (%s). %s", buffer.name.c_str(), Log::HrToString(hr).c_str());
            return BufferHandle{};
        }
        void* mapped = nullptr;
        const D3D12_RANGE noRead = { 0, 0 };
        if (FAILED(upload->Map(0, &noRead, &mapped))) return BufferHandle{};
        std::memcpy(mapped, initialData, descIn.size);
        upload->Unmap(0, nullptr);
        ID3D12Resource* dst = buffer.resource.Get();
        ID3D12Resource* src = upload.Get();
        const uint64_t bytes = descIn.size;
        ExecuteUploadSync([dst, src, bytes](ID3D12GraphicsCommandList* list)
        {
            list->CopyBufferRegion(dst, 0, src, 0, bytes);
        });
    }
    return m_buffers.Add(std::move(buffer));
}

void D3D12Device::DestroyBuffer(BufferHandle handle)
{
    D3D12Buffer* buffer = m_buffers.Get(handle);
    if (buffer == nullptr) return;
    if (buffer->resource) DeferRelease(buffer->resource);
    m_buffers.Remove(handle);
}

void D3D12Device::UpdateBuffer(BufferHandle handle, const void* data, uint32_t size)
{
    static bool warnedInvalid = false;
    D3D12Buffer* buffer = m_buffers.Get(handle);
    if (buffer == nullptr || data == nullptr)
    {
        ErrorOnce(warnedInvalid, "UpdateBuffer : 유효하지 않은 핸들 또는 데이터.");
        return;
    }
    if (size > buffer->desc.size)
    {
        Log::Error("UpdateBuffer : 크기 초과 (%s, %u > %u).", buffer->name.c_str(), size, buffer->desc.size);
        return;
    }

    switch (buffer->desc.usage)
    {
    case BufferUsage::Dynamic:
    {
        D3D12_GPU_VIRTUAL_ADDRESS address = 0;
        uint8_t* dst = AllocateUpload(buffer->desc.size, address);
        if (dst == nullptr) return;
        std::memcpy(dst, data, size);
        buffer->gpuAddress = address;
        buffer->updatedFrame = m_frameCounter;
        break;
    }
    case BufferUsage::Default:
    {
        if (size != buffer->desc.size)
        {
            Log::Error("UpdateBuffer : Default 버퍼는 전체 크기로만 갱신할 수 있음 (%s, %u != %u).", buffer->name.c_str(), size, buffer->desc.size);
            return;
        }
        // 드문 경로(Renderer 는 쓰지 않는다). 동기 업로드로 정확성만 보장한다.
        ComPtr<ID3D12Resource> upload;
        const D3D12_HEAP_PROPERTIES uploadHeap = D3D12Convert::HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC rd = D3D12Convert::BufferDesc(buffer->desc.size);
        if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload.GetAddressOf())))) return;
        void* mapped = nullptr;
        const D3D12_RANGE noRead = { 0, 0 };
        if (FAILED(upload->Map(0, &noRead, &mapped))) return;
        std::memcpy(mapped, data, size);
        upload->Unmap(0, nullptr);
        ID3D12Resource* dst = buffer->resource.Get();
        ID3D12Resource* src = upload.Get();
        const uint64_t bytes = size;
        ExecuteUploadSync([dst, src, bytes](ID3D12GraphicsCommandList* list) { list->CopyBufferRegion(dst, 0, src, 0, bytes); });
        break;
    }
    case BufferUsage::Staging:
        Log::Error("UpdateBuffer : Staging 버퍼는 이 경로로 갱신하지 않음 (%s).", buffer->name.c_str());
        break;
    }
}

// ------------------------------------------------------------------ 텍스처

TextureHandle D3D12Device::CreateTexture(const TextureDesc& descIn, const TextureSubresource* subresources, uint32_t subresourceCount)
{
    if (!m_device)
    {
        Log::Error("CreateTexture : 장치가 없음.");
        return TextureHandle{};
    }

    D3D12Texture texture;
    texture.desc = descIn;
    texture.desc.debugName = nullptr;
    texture.name = descIn.debugName ? descIn.debugName : "";

    const DXGI_FORMAT format = D3D12Convert::ToDXGI(descIn.format);
    const bool isDepth = (descIn.bindFlags & TextureBind_DepthStencil) != 0;
    const bool isSrv = (descIn.bindFlags & TextureBind_ShaderResource) != 0;
    const bool isRt = (descIn.bindFlags & TextureBind_RenderTarget) != 0;
    // 11단계: 렌더 타깃 + SRV 인 UNORM 색 텍스처(에디터 씬 뷰)도 TYPELESS — sRGB RTV 와 UNORM SRV 를 함께 만든다.
    const bool colorRtAndSrv = isRt && isSrv && format == DXGI_FORMAT_R8G8B8A8_UNORM;
    texture.typeless = (isDepth && isSrv && D3D12Convert::IsDepthFormat(format)) || colorRtAndSrv;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Alignment = 0;
    rd.Width = descIn.width;
    rd.Height = descIn.height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = static_cast<UINT16>(descIn.mipLevels);
    rd.Format = texture.typeless ? (isDepth ? D3D12Convert::DepthTypeless(format) : DXGI_FORMAT_R8G8B8A8_TYPELESS) : format;
    rd.SampleDesc.Count = descIn.sampleCount;
    rd.SampleDesc.Quality = 0;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (isRt) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (isDepth) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if (isDepth && !isSrv) rd.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    // 최적화 클리어 값: 깊이 1.0. 매 프레임 이 값으로 클리어하므로 일치한다.
    D3D12_CLEAR_VALUE clear = {};
    const D3D12_CLEAR_VALUE* pClear = nullptr;
    if (isDepth)
    {
        clear.Format = format;
        clear.DepthStencil.Depth = 1.0f;
        clear.DepthStencil.Stencil = 0;
        pClear = &clear;
    }
    else if (isRt)
    {
        clear.Format = format;
        clear.Color[3] = 1.0f;
        pClear = &clear;
    }

    // 초기 상태. 초기 데이터가 있으면 COPY_DEST, 깊이 전용이면 DEPTH_WRITE (Renderer 가 배리어를 걸지 않는 메인 깊이 버퍼),
    // 그 외(그림자 맵 포함)는 COMMON — Renderer 의 첫 배리어가 Common → DepthWrite 이다 (6단계의 추적과 같은 출발점).
    const bool hasInit = subresources != nullptr && subresourceCount > 0;
    D3D12_RESOURCE_STATES initial = D3D12_RESOURCE_STATE_COMMON;
    if (hasInit) initial = D3D12_RESOURCE_STATE_COPY_DEST;
    else if (isDepth && !isSrv) initial = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    texture.state = (isDepth && !isSrv) ? ResourceState::DepthWrite : ResourceState::Common;

    const D3D12_HEAP_PROPERTIES heap = D3D12Convert::HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    HRESULT hr = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, initial, pClear, IID_PPV_ARGS(texture.resource.GetAddressOf()));
    if (FAILED(hr))
    {
        Log::Error("CreateTexture 실패 (%s, %ux%u, 밉 %u). %s", texture.name.c_str(), descIn.width, descIn.height, descIn.mipLevels, Log::HrToString(hr).c_str());
        return TextureHandle{};
    }
    SetDebugName(texture.resource.Get(), texture.name);

    if (hasInit)
    {
        if (subresourceCount < descIn.mipLevels)
        {
            Log::Error("CreateTexture : 초기 데이터가 밉 레벨 수보다 적음 (%s, %u < %u).", texture.name.c_str(), subresourceCount, descIn.mipLevels);
            return TextureHandle{};
        }
        // 업로드 힙 → CopyTextureRegion(밉마다) → 배리어. D3D11 의 D3D11_SUBRESOURCE_DATA[] 가 여기서 이렇게 풀린다.
        const UINT mips = descIn.mipLevels;
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(mips);
        std::vector<UINT> numRows(mips);
        std::vector<UINT64> rowSizes(mips);
        UINT64 totalBytes = 0;
        m_device->GetCopyableFootprints(&rd, 0, mips, 0, layouts.data(), numRows.data(), rowSizes.data(), &totalBytes);

        ComPtr<ID3D12Resource> upload;
        const D3D12_HEAP_PROPERTIES uploadHeap = D3D12Convert::HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC ud = D3D12Convert::BufferDesc(totalBytes);
        hr = m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(upload.GetAddressOf()));
        if (FAILED(hr))
        {
            Log::Error("CreateTexture : 업로드 버퍼 생성 실패 (%s). %s", texture.name.c_str(), Log::HrToString(hr).c_str());
            return TextureHandle{};
        }
        uint8_t* mapped = nullptr;
        const D3D12_RANGE noRead = { 0, 0 };
        if (FAILED(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)))) return TextureHandle{};
        for (UINT mip = 0; mip < mips; ++mip)
        {
            const uint8_t* src = static_cast<const uint8_t*>(subresources[mip].data);
            uint8_t* dst = mapped + layouts[mip].Offset;
            for (UINT row = 0; row < numRows[mip]; ++row)
            {
                // 업로드 힙의 행 간격은 256 정렬(RowPitch) 이다. 원본 행 간격과 다르므로 행 단위로 복사한다.
                std::memcpy(dst + static_cast<size_t>(row) * layouts[mip].Footprint.RowPitch,
                    src + static_cast<size_t>(row) * subresources[mip].rowPitch,
                    static_cast<size_t>(rowSizes[mip]));
            }
        }
        upload->Unmap(0, nullptr);

        ID3D12Resource* dstRes = texture.resource.Get();
        ID3D12Resource* srcRes = upload.Get();
        ExecuteUploadSync([&](ID3D12GraphicsCommandList* list)
        {
            for (UINT mip = 0; mip < mips; ++mip)
            {
                D3D12_TEXTURE_COPY_LOCATION dst = {};
                dst.pResource = dstRes;
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.SubresourceIndex = mip;
                D3D12_TEXTURE_COPY_LOCATION src = {};
                src.pResource = srcRes;
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint = layouts[mip];
                list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            }
            const D3D12_RESOURCE_BARRIER barrier = D3D12Convert::TransitionBarrier(dstRes,
                D3D12_RESOURCE_STATE_COPY_DEST, D3D12Convert::ToD3D12(ResourceState::ShaderResource));
            list->ResourceBarrier(1, &barrier);
        });
        texture.state = ResourceState::ShaderResource;
    }

    return m_textures.Add(std::move(texture));
}

void D3D12Device::DestroyTexture(TextureHandle handle)
{
    D3D12Texture* texture = m_textures.Get(handle);
    if (texture == nullptr) return;
    // GPU 가 이전 프레임에서 아직 쓰고 있을 수 있다. 리소스와 디스크립터 모두 펜스 뒤에 놓는다.
    if (texture->resource) DeferRelease(texture->resource);
    DeferFree(&m_rtvHeap, texture->rtv, 1);
    DeferFree(&m_rtvHeap, texture->rtvSrgb, 1);
    DeferFree(&m_dsvHeap, texture->dsv, 1);
    DeferFree(&m_srvStaging, texture->srv, 1);
    DeferFree(&m_srvHeap, texture->srvVisible, 1);
    m_textures.Remove(handle);
}

void D3D12Device::ReleaseTextureNow(TextureHandle handle)
{
    D3D12Texture* texture = m_textures.Get(handle);
    if (texture == nullptr) return;
    m_rtvHeap.Free(texture->rtv, 1);
    m_rtvHeap.Free(texture->rtvSrgb, 1);
    m_dsvHeap.Free(texture->dsv, 1);
    m_srvStaging.Free(texture->srv, 1);
    m_srvHeap.Free(texture->srvVisible, 1);
    m_textures.Remove(handle);   // ComPtr 즉시 해제
}

bool D3D12Device::CreateBackBufferTextures()
{
    for (uint32_t i = 0; i < kBackBufferCount; ++i)
    {
        D3D12Texture texture;
        const HRESULT hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(texture.resource.GetAddressOf()));
        if (FAILED(hr))
        {
            Log::Error("SwapChain 백버퍼 %u 가져오기 실패. %s", i, Log::HrToString(hr).c_str());
            return false;
        }
        texture.desc.width = m_screenWidth;
        texture.desc.height = m_screenHeight;
        texture.desc.format = Format::R8G8B8A8_UNORM;
        texture.desc.mipLevels = 1;
        texture.desc.bindFlags = TextureBind_RenderTarget;
        texture.desc.sampleCount = 1;
        texture.name = "BackBuffer[" + std::to_string(i) + "]";
        texture.state = ResourceState::Present;   // 스왑체인 버퍼는 COMMON(=PRESENT) 으로 시작한다
        SetDebugName(texture.resource.Get(), texture.name);
        m_backBuffers[i] = m_textures.Add(std::move(texture));
    }
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    return true;
}

bool D3D12Device::CreateDepthTexture()
{
    TextureDesc desc;
    desc.width = m_screenWidth;
    desc.height = m_screenHeight;
    desc.format = Format::D24_UNORM_S8_UINT;
    desc.mipLevels = 1;
    desc.bindFlags = TextureBind_DepthStencil;
    desc.sampleCount = 1;
    desc.debugName = "DepthBuffer";
    m_depthBuffer = CreateTexture(desc);
    return m_depthBuffer.IsValid();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::GetRTV(D3D12Texture& texture, bool srgbView)
{
    uint32_t& index = srgbView ? texture.rtvSrgb : texture.rtv;
    if (index == D3D12DescriptorHeap::kInvalid)
    {
        index = m_rtvHeap.Allocate(1);
        D3D12_RENDER_TARGET_VIEW_DESC vd = {};
        const DXGI_FORMAT format = D3D12Convert::ToDXGI(texture.desc.format);
        vd.Format = srgbView ? D3D12Convert::ToSrgb(format) : format;   // 5단계: UNORM 백버퍼의 sRGB 뷰
        vd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        vd.Texture2D.MipSlice = 0;
        vd.Texture2D.PlaneSlice = 0;
        m_device->CreateRenderTargetView(texture.resource.Get(), &vd, m_rtvHeap.Cpu(index));
    }
    return m_rtvHeap.Cpu(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::GetDSV(D3D12Texture& texture)
{
    if (texture.dsv == D3D12DescriptorHeap::kInvalid)
    {
        texture.dsv = m_dsvHeap.Allocate(1);
        D3D12_DEPTH_STENCIL_VIEW_DESC vd = {};
        vd.Format = D3D12Convert::ToDXGI(texture.desc.format);   // TYPELESS 리소스에도 뷰는 D32_FLOAT
        vd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        vd.Flags = D3D12_DSV_FLAG_NONE;
        vd.Texture2D.MipSlice = 0;
        m_device->CreateDepthStencilView(texture.resource.Get(), &vd, m_dsvHeap.Cpu(texture.dsv));
    }
    return m_dsvHeap.Cpu(texture.dsv);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::GetSRV(D3D12Texture& texture)
{
    if (texture.srv == D3D12DescriptorHeap::kInvalid)
    {
        texture.srv = m_srvStaging.Allocate(1);
        D3D12_SHADER_RESOURCE_VIEW_DESC vd = {};
        const DXGI_FORMAT format = D3D12Convert::ToDXGI(texture.desc.format);
        vd.Format = texture.typeless ? D3D12Convert::DepthShaderView(format) : format;   // D32 → R32_FLOAT
        vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        vd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        vd.Texture2D.MostDetailedMip = 0;
        vd.Texture2D.MipLevels = texture.desc.mipLevels;
        vd.Texture2D.PlaneSlice = 0;
        vd.Texture2D.ResourceMinLODClamp = 0.0f;
        m_device->CreateShaderResourceView(texture.resource.Get(), &vd, m_srvStaging.Cpu(texture.srv));
    }
    return m_srvStaging.Cpu(texture.srv);
}

// ------------------------------------------------------------------ 샘플러 / 셰이더 / PSO

SamplerHandle D3D12Device::CreateSampler(const SamplerDesc& descIn)
{
    if (!m_device) return SamplerHandle{};
    D3D12Sampler sampler;
    sampler.desc = descIn;
    sampler.desc.debugName = nullptr;
    sampler.name = descIn.debugName ? descIn.debugName : "";
    sampler.staging = m_samplerStaging.Allocate(1);
    if (sampler.staging == D3D12DescriptorHeap::kInvalid)
    {
        Log::Error("CreateSampler : 스테이징 샘플러 힙이 가득 참 (%s).", sampler.name.c_str());
        return SamplerHandle{};
    }
    D3D12_SAMPLER_DESC sd = {};
    sd.Filter = D3D12Convert::ToD3D12(descIn.filter);
    sd.AddressU = D3D12Convert::ToD3D12(descIn.addressU);
    sd.AddressV = D3D12Convert::ToD3D12(descIn.addressV);
    sd.AddressW = D3D12Convert::ToD3D12(descIn.addressW);
    sd.MipLODBias = 0.0f;
    sd.MaxAnisotropy = descIn.maxAnisotropy;
    // 비교 필터가 아니면 ComparisonFunc 는 무시되지만 Debug Layer 가 "의도가 아닐 것"이라 경고한다. NEVER 로 비운다.
    sd.ComparisonFunc = (descIn.filter == SamplerFilter::Comparison) ? D3D12Convert::ToD3D12(descIn.compareFunc) : D3D12_COMPARISON_FUNC_NEVER;
    std::memcpy(sd.BorderColor, descIn.borderColor, sizeof(sd.BorderColor));
    sd.MinLOD = descIn.minLod;
    sd.MaxLOD = descIn.maxLod;
    m_device->CreateSampler(&sd, m_samplerStaging.Cpu(sampler.staging));
    return m_samplers.Add(std::move(sampler));
}

void D3D12Device::DestroySampler(SamplerHandle handle)
{
    D3D12Sampler* sampler = m_samplers.Get(handle);
    if (sampler == nullptr) return;
    DeferFree(&m_samplerStaging, sampler->staging, 1);
    m_samplers.Remove(handle);
}

ShaderHandle D3D12Device::CreateShader(const ShaderDesc& desc)
{
    if (desc.bytecode == nullptr || desc.bytecodeSize == 0)
    {
        Log::Error("CreateShader : 바이트코드가 비어 있음.");
        return ShaderHandle{};
    }
    // D3D12 에는 셰이더 객체가 없다. 바이트코드(DXBC)를 보관했다가 PSO 생성 시 넘긴다.
    D3D12Shader shader;
    shader.stage = desc.stage;
    shader.name = desc.debugName ? desc.debugName : "";
    const uint8_t* bytes = static_cast<const uint8_t*>(desc.bytecode);
    shader.bytecode.assign(bytes, bytes + desc.bytecodeSize);
    return m_shaders.Add(std::move(shader));
}

void D3D12Device::DestroyShader(ShaderHandle handle)
{
    m_shaders.Remove(handle);   // PSO 는 바이트코드 사본을 이미 컴파일했으므로 참조가 남지 않는다
}

PipelineHandle D3D12Device::CreatePipeline(const PipelineStateDesc& desc)
{
    return m_pipelines.GetOrCreate(*this, desc);
}

void D3D12Device::InvalidatePipelines()
{
    m_pipelines.Clear(*this);
}

std::shared_ptr<D3D12RootLayout> D3D12Device::GetOrCreateRootLayout(const PipelineStateDesc& desc)
{
    uint64_t key = 0xCBF29CE484222325ull ^ desc.bindingLayoutCount;
    for (uint32_t i = 0; i < desc.bindingLayoutCount; ++i)
    {
        key = key * 1099511628211ull ^ desc.bindingLayouts[i].index;
        key = key * 1099511628211ull ^ desc.bindingLayouts[i].generation;
    }
    auto found = m_rootLayouts.find(key);
    if (found != m_rootLayouts.end()) return found->second;

    auto layoutSet = std::make_shared<D3D12RootLayout>();
    layoutSet->key = key;
    layoutSet->setCount = desc.bindingLayoutCount;

    std::vector<D3D12_ROOT_PARAMETER> params;
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> ranges;   // 테이블마다 하나. 포인터가 흔들리지 않게 미리 잡는다
    ranges.reserve(static_cast<size_t>(desc.bindingLayoutCount) * 2);

    for (uint32_t s = 0; s < desc.bindingLayoutCount; ++s)
    {
        const D3D12BindingLayout* layout = m_bindingLayouts.Get(desc.bindingLayouts[s]);
        if (layout == nullptr)
        {
            Log::Error("루트 시그니처 : 셋 %u 의 BindingLayoutHandle 이 유효하지 않음.", s);
            return nullptr;
        }
        D3D12RootLayout::SetBinding& sb = layoutSet->sets[s];
        sb.layout = desc.bindingLayouts[s];
        for (uint32_t& r : sb.cbvRoot) r = UINT32_MAX;

        std::vector<D3D12_DESCRIPTOR_RANGE> srvRanges, samplerRanges;
        uint8_t srvMask = 0, samplerMask = 0;
        for (uint32_t i = 0; i < layout->desc.slotCount; ++i)
        {
            const BindingSlot& slot = layout->desc.slots[i];
            switch (slot.type)
            {
            case BindingType::ConstantBuffer:
            {
                // 루트 CBV: 드로우마다 GPU 주소를 건다. 4단계가 "CBV 는 루트 디스크립터"라고 정한 그것.
                D3D12_ROOT_PARAMETER p = {};
                p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
                p.Descriptor.ShaderRegister = slot.reg;
                p.Descriptor.RegisterSpace = 0;
                p.ShaderVisibility = VisibilityOf(slot.stageMask);
                sb.cbvRoot[i] = static_cast<uint32_t>(params.size());
                params.push_back(p);
                break;
            }
            case BindingType::ShaderResource:
            {
                D3D12_DESCRIPTOR_RANGE r = {};
                r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                r.NumDescriptors = 1;
                r.BaseShaderRegister = slot.reg;
                r.RegisterSpace = 0;
                r.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;   // 셋의 영구 범위와 같은 순서
                srvRanges.push_back(r);
                srvMask |= slot.stageMask;
                break;
            }
            case BindingType::Sampler:
            {
                D3D12_DESCRIPTOR_RANGE r = {};
                r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                r.NumDescriptors = 1;
                r.BaseShaderRegister = slot.reg;
                r.RegisterSpace = 0;
                r.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                samplerRanges.push_back(r);
                samplerMask |= slot.stageMask;
                break;
            }
            }
        }
        if (!srvRanges.empty())
        {
            ranges.push_back(std::move(srvRanges));
            D3D12_ROOT_PARAMETER p = {};
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(ranges.back().size());
            p.DescriptorTable.pDescriptorRanges = ranges.back().data();
            p.ShaderVisibility = VisibilityOf(srvMask);
            sb.srvTableRoot = static_cast<uint32_t>(params.size());
            params.push_back(p);
        }
        if (!samplerRanges.empty())
        {
            ranges.push_back(std::move(samplerRanges));
            D3D12_ROOT_PARAMETER p = {};
            p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            p.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(ranges.back().size());
            p.DescriptorTable.pDescriptorRanges = ranges.back().data();
            p.ShaderVisibility = VisibilityOf(samplerMask);
            sb.samplerTableRoot = static_cast<uint32_t>(params.size());
            params.push_back(p);
        }
    }

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = static_cast<UINT>(params.size());
    rsd.pParameters = params.empty() ? nullptr : params.data();
    rsd.NumStaticSamplers = 0;
    rsd.pStaticSamplers = nullptr;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, error;
    HRESULT hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, blob.GetAddressOf(), error.GetAddressOf());
    if (FAILED(hr))
    {
        Log::Error("D3D12SerializeRootSignature 실패: %s", error ? static_cast<const char*>(error->GetBufferPointer()) : Log::HrToString(hr).c_str());
        return nullptr;
    }
    hr = m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(layoutSet->rootSignature.GetAddressOf()));
    if (FAILED(hr))
    {
        Log::Error("CreateRootSignature 실패. %s", Log::HrToString(hr).c_str());
        return nullptr;
    }
    SetDebugName(layoutSet->rootSignature.Get(), "RootSignature");
    Log::Info("루트 시그니처 생성: 셋 %u개, 루트 파라미터 %u개 (CBV 루트 디스크립터 + SRV/샘플러 테이블)", desc.bindingLayoutCount, rsd.NumParameters);
    m_rootLayouts.emplace(key, layoutSet);
    return layoutSet;
}

// ------------------------------------------------------------------ 바인딩 레이아웃 / 리소스 셋

BindingLayoutHandle D3D12Device::CreateBindingLayout(const BindingLayoutDesc& descIn)
{
    const char* name = descIn.debugName ? descIn.debugName : "";
    if (descIn.slotCount > kMaxBindingSlots)
    {
        Log::Error("CreateBindingLayout : 슬롯이 너무 많음 (%s, %u > %u).", name, descIn.slotCount, kMaxBindingSlots);
        return BindingLayoutHandle{};
    }
    for (uint32_t i = 0; i < descIn.slotCount; ++i)
    {
        const BindingSlot& slot = descIn.slots[i];
        if (slot.stageMask == 0)
        {
            Log::Error("CreateBindingLayout : 슬롯 %u 의 stageMask가 0 (%s).", i, name);
            return BindingLayoutHandle{};
        }
        uint8_t limit = 0;
        switch (slot.type)
        {
        case BindingType::ConstantBuffer: limit = kMaxCbvRegister; break;
        case BindingType::ShaderResource: limit = kMaxSrvRegister; break;
        case BindingType::Sampler:        limit = kMaxSamplerRegister; break;
        }
        if (slot.reg >= limit)
        {
            Log::Error("CreateBindingLayout : 슬롯 %u 레지스터 %u 가 상한 %u 이상 (%s).", i, slot.reg, limit, name);
            return BindingLayoutHandle{};
        }
        for (uint32_t j = 0; j < i; ++j)
        {
            const BindingSlot& other = descIn.slots[j];
            if (other.type == slot.type && other.reg == slot.reg && (other.stageMask & slot.stageMask) != 0)
            {
                Log::Error("CreateBindingLayout : 슬롯 %u 와 %u 가 같은 레지스터를 같은 스테이지에 선언 (%s).", j, i, name);
                return BindingLayoutHandle{};
            }
        }
    }
    D3D12BindingLayout layout;
    layout.desc = descIn;
    layout.desc.debugName = nullptr;
    layout.name = name;
    return m_bindingLayouts.Add(std::move(layout));
}

void D3D12Device::DestroyBindingLayout(BindingLayoutHandle handle)
{
    m_bindingLayouts.Remove(handle);
}

ResourceSetHandle D3D12Device::CreateResourceSet(const ResourceSetDesc& descIn)
{
    const char* name = descIn.debugName ? descIn.debugName : "";
    const D3D12BindingLayout* layout = m_bindingLayouts.Get(descIn.layout);
    if (layout == nullptr)
    {
        Log::Error("CreateResourceSet : 유효하지 않은 BindingLayoutHandle (%s).", name);
        return ResourceSetHandle{};
    }

    // 검증 + 테이블 크기 계산
    uint32_t srvCount = 0, samplerCount = 0;
    for (uint32_t i = 0; i < layout->desc.slotCount; ++i)
    {
        const BindingSlot& slot = layout->desc.slots[i];
        const ResourceBinding& binding = descIn.bindings[i];
        switch (slot.type)
        {
        case BindingType::ConstantBuffer:
        {
            const D3D12Buffer* buffer = m_buffers.Get(binding.buffer);
            if (buffer == nullptr || !(buffer->desc.bindFlags & BufferBind_Constant))
            {
                Log::Error("CreateResourceSet : 슬롯 %u (b%u) 에 상수버퍼가 아닌 핸들 (%s).", i, slot.reg, name);
                return ResourceSetHandle{};
            }
            break;
        }
        case BindingType::ShaderResource:
        {
            const D3D12Texture* texture = m_textures.Get(binding.texture);
            if (texture == nullptr || !(texture->desc.bindFlags & TextureBind_ShaderResource))
            {
                Log::Error("CreateResourceSet : 슬롯 %u (t%u) 에 SRV 가능한 텍스처가 아닌 핸들 (%s).", i, slot.reg, name);
                return ResourceSetHandle{};
            }
            ++srvCount;
            break;
        }
        case BindingType::Sampler:
            if (m_samplers.Get(binding.sampler) == nullptr)
            {
                Log::Error("CreateResourceSet : 슬롯 %u (s%u) 에 유효하지 않은 샘플러 핸들 (%s).", i, slot.reg, name);
                return ResourceSetHandle{};
            }
            ++samplerCount;
            break;
        }
    }

    // 디스크립터 테이블: 셰이더 가시 힙의 영구 범위. 슬롯 순서 = 루트 시그니처 range 순서.
    D3D12ResourceSet set;
    set.desc = descIn;
    set.desc.debugName = nullptr;
    set.name = name;
    set.srvCount = srvCount;
    set.samplerCount = samplerCount;
    if (srvCount > 0)
    {
        set.srvTableStart = m_srvHeap.Allocate(srvCount);
        if (set.srvTableStart == D3D12DescriptorHeap::kInvalid) { Log::Error("CreateResourceSet : SRV 힙 부족 (%s).", name); return ResourceSetHandle{}; }
    }
    if (samplerCount > 0)
    {
        set.samplerTableStart = m_samplerHeap.Allocate(samplerCount);
        if (set.samplerTableStart == D3D12DescriptorHeap::kInvalid) { Log::Error("CreateResourceSet : 샘플러 힙 부족 (%s).", name); m_srvHeap.Free(set.srvTableStart, srvCount); return ResourceSetHandle{}; }
    }
    uint32_t srvOffset = 0, samplerOffset = 0;
    for (uint32_t i = 0; i < layout->desc.slotCount; ++i)
    {
        const BindingSlot& slot = layout->desc.slots[i];
        const ResourceBinding& binding = descIn.bindings[i];
        if (slot.type == BindingType::ShaderResource)
        {
            D3D12Texture* texture = m_textures.Get(binding.texture);
            m_device->CopyDescriptorsSimple(1, m_srvHeap.Cpu(set.srvTableStart + srvOffset++), GetSRV(*texture), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        else if (slot.type == BindingType::Sampler)
        {
            const D3D12Sampler* sampler = m_samplers.Get(binding.sampler);
            m_device->CopyDescriptorsSimple(1, m_samplerHeap.Cpu(set.samplerTableStart + samplerOffset++), m_samplerStaging.Cpu(sampler->staging), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        }
    }
    return m_resourceSets.Add(std::move(set));
}

void D3D12Device::DestroyResourceSet(ResourceSetHandle handle)
{
    D3D12ResourceSet* set = m_resourceSets.Get(handle);
    if (set == nullptr) return;
    // 이전 프레임의 드로우가 이 테이블을 아직 읽고 있을 수 있다.
    DeferFree(&m_srvHeap, set->srvTableStart, set->srvCount);
    DeferFree(&m_samplerHeap, set->samplerTableStart, set->samplerCount);
    m_resourceSets.Remove(handle);
}

// ------------------------------------------------------------------ ImGui 어댑터 (imgui_impl_dx12)

void D3D12Device::ImGuiSrvAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
{
    D3D12Device* self = static_cast<D3D12Device*>(info->UserData);
    const uint32_t index = self->m_srvHeap.Allocate(1);
    *outCpu = self->m_srvHeap.Cpu(index);
    *outGpu = self->m_srvHeap.Gpu(index);
}

void D3D12Device::ImGuiSrvFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
{
    (void)cpu;
    D3D12Device* self = static_cast<D3D12Device*>(info->UserData);
    const uint32_t index = static_cast<uint32_t>((gpu.ptr - self->m_srvHeap.Gpu(0).ptr) / self->m_srvHeap.GetIncrement());
    self->DeferFree(&self->m_srvHeap, index, 1);
}

bool D3D12Device::InitImGui()
{
    // imgui_impl_dx12 는 셰이더 가시 SRV 힙 하나와 그 안의 슬롯 할당기를 요구한다. 우리 힙을 그대로 준다 —
    // 그래야 프레임에 힙이 하나만 바인딩되고, ImGui 가 SetDescriptorHeaps 를 불러도 상태가 바뀌지 않는다.
    ImGui_ImplDX12_InitInfo info = {};
    info.Device = m_device.Get();
    info.CommandQueue = m_queue.Get();
    info.NumFramesInFlight = kFrameCount;
    info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;   // UI 패스는 UNORM 뷰
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.UserData = this;
    info.SrvDescriptorHeap = m_srvHeap.Get();
    info.SrvDescriptorAllocFn = &D3D12Device::ImGuiSrvAlloc;
    info.SrvDescriptorFreeFn = &D3D12Device::ImGuiSrvFree;
    if (!ImGui_ImplDX12_Init(&info))
    {
        Log::Error("ImGui DX12 백엔드 초기화 실패");
        return false;
    }
    m_imguiInitialized = true;
    return true;
}

void D3D12Device::NewFrameImGui()
{
    if (m_imguiInitialized) ImGui_ImplDX12_NewFrame();
}

void D3D12Device::RenderImGui()
{
    if (m_imguiInitialized && m_list) ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_list.Get());
}

void D3D12Device::ShutdownImGui()
{
    if (!m_imguiInitialized) return;
    ImGui_ImplDX12_Shutdown();
    m_imguiInitialized = false;
}

uint64_t D3D12Device::GetImGuiTextureId(TextureHandle handle)
{
    D3D12Texture* texture = m_textures.Get(handle);
    if (texture == nullptr || !texture->resource) return 0;
    if (texture->srvVisible == D3D12DescriptorHeap::kInvalid)
    {
        texture->srvVisible = m_srvHeap.Allocate(1);
        m_device->CopyDescriptorsSimple(1, m_srvHeap.Cpu(texture->srvVisible), GetSRV(*texture), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    // imgui_impl_dx12 의 ImTextureID 는 셰이더 가시 힙의 GPU 핸들 값이다.
    return m_srvHeap.Gpu(texture->srvVisible).ptr;
}

// ------------------------------------------------------------------ 10단계: 타임스탬프

bool D3D12Device::CreateTimestampResources()
{
    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = kFrameCount * kMaxTimestamps;
    HRESULT hr = m_device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(m_timestampHeap.GetAddressOf()));
    if (FAILED(hr)) { Log::Error("CreateQueryHeap(TIMESTAMP) 실패. %s", Log::HrToString(hr).c_str()); return false; }
    SetDebugName(m_timestampHeap.Get(), "TimestampHeap");

    const D3D12_HEAP_PROPERTIES heap = D3D12Convert::HeapProperties(D3D12_HEAP_TYPE_READBACK);
    const D3D12_RESOURCE_DESC desc = D3D12Convert::BufferDesc(static_cast<UINT64>(kFrameCount) * kMaxTimestamps * sizeof(uint64_t));
    hr = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(m_timestampReadback.GetAddressOf()));
    if (FAILED(hr)) { Log::Error("타임스탬프 리드백 버퍼 생성 실패. %s", Log::HrToString(hr).c_str()); return false; }
    SetDebugName(m_timestampReadback.Get(), "TimestampReadback");
    for (uint32_t i = 0; i < kFrameCount; ++i) m_timestampWritten[i] = 0;
    return true;
}

void D3D12Device::WriteTimestamp(uint32_t slot)
{
    if (slot >= kMaxTimestamps || !m_timestampHeap || !m_list) return;
    m_list->EndQuery(m_timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_frameIndex * kMaxTimestamps + slot);
    if (slot + 1 > m_timestampWritten[m_frameIndex]) m_timestampWritten[m_frameIndex] = slot + 1;
}

void D3D12Device::ReadTimestamps(uint32_t slot)
{
    // BeginFrame 이 이 슬롯의 펜스를 기다린 직후에 부른다. 리드백 버퍼는 READBACK 힙이라 Map 이 곧 읽기다.
    const uint32_t written = m_timestampWritten[slot];
    if (!m_timestampReadback || written == 0)
    {
        m_timestampWritten[slot] = 0;
        return;
    }
    const D3D12_RANGE readRange = { static_cast<SIZE_T>(slot) * kMaxTimestamps * sizeof(uint64_t),
                                    static_cast<SIZE_T>(slot) * kMaxTimestamps * sizeof(uint64_t) + written * sizeof(uint64_t) };
    void* mapped = nullptr;
    if (SUCCEEDED(m_timestampReadback->Map(0, &readRange, &mapped)) && mapped != nullptr)
    {
        const uint64_t* values = reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(mapped) + readRange.Begin);
        for (uint32_t i = 0; i < kMaxTimestamps; ++i) m_lastTicks[i] = i < written ? values[i] : 0;
        const D3D12_RANGE noWrite = { 0, 0 };
        m_timestampReadback->Unmap(0, &noWrite);
        if (m_lastFrequency == 0) m_queue->GetTimestampFrequency(&m_lastFrequency);
        m_lastFrameNumber = m_timestampFrame[slot];
        m_hasTimestampResults = m_lastFrequency != 0;
    }
    m_timestampWritten[slot] = 0;
}

bool D3D12Device::GetTimestampResults(uint64_t* ticks, uint32_t count, uint64_t& frequency, uint64_t& frameNumber)
{
    if (!m_hasTimestampResults) return false;
    const uint32_t n = count < kMaxTimestamps ? count : kMaxTimestamps;
    for (uint32_t i = 0; i < n; ++i) ticks[i] = m_lastTicks[i];
    frequency = m_lastFrequency;
    frameNumber = m_lastFrameNumber;
    return true;
}

// ------------------------------------------------------------------ 11단계: 백버퍼 리드백 (스크린샷)

void D3D12Device::RecordBackBufferReadback()
{
    m_readbackRequested = false;
    D3D12Texture* texture = m_textures.Get(m_backBuffers[m_backBufferIndex]);
    if (texture == nullptr || !texture->resource) return;

    const D3D12_RESOURCE_DESC desc = texture->resource->GetDesc();
    UINT64 total = 0;
    m_device->GetCopyableFootprints(&desc, 0, 1, 0, &m_readbackFootprint, nullptr, nullptr, &total);
    if (!m_readbackBuffer || m_readbackBuffer->GetDesc().Width < total)
    {
        m_readbackBuffer.Reset();
        const D3D12_HEAP_PROPERTIES heap = D3D12Convert::HeapProperties(D3D12_HEAP_TYPE_READBACK);
        const D3D12_RESOURCE_DESC rd = D3D12Convert::BufferDesc(total);
        const HRESULT hr = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(m_readbackBuffer.GetAddressOf()));
        if (FAILED(hr)) { Log::Error("리드백 버퍼 생성 실패. %s", Log::HrToString(hr).c_str()); return; }
        SetDebugName(m_readbackBuffer.Get(), "BackBufferReadback");
    }

    // 백버퍼는 UI 패스 뒤 PRESENT 상태다 (Renderer::EndUIPass). COPY_SOURCE 로 갔다가 되돌린다.
    D3D12_RESOURCE_BARRIER toCopy = D3D12Convert::TransitionBarrier(texture->resource.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_list->ResourceBarrier(1, &toCopy);
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = m_readbackBuffer.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = m_readbackFootprint;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = texture->resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    m_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER toPresent = D3D12Convert::TransitionBarrier(texture->resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    m_list->ResourceBarrier(1, &toPresent);
    m_readbackWidth = static_cast<uint32_t>(desc.Width);
    m_readbackHeight = desc.Height;
    m_readbackPending = true;
}

void D3D12Device::FinishBackBufferReadback()
{
    m_readbackPending = false;
    WaitForGpu();
    void* mapped = nullptr;
    const D3D12_RANGE range = { 0, static_cast<SIZE_T>(m_readbackFootprint.Footprint.RowPitch) * m_readbackHeight };
    if (FAILED(m_readbackBuffer->Map(0, &range, &mapped)) || mapped == nullptr) return;
    m_readbackPixels.resize(static_cast<size_t>(m_readbackWidth) * m_readbackHeight * 4);
    for (uint32_t y = 0; y < m_readbackHeight; ++y)
    {
        std::memcpy(m_readbackPixels.data() + static_cast<size_t>(y) * m_readbackWidth * 4,
            static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * m_readbackFootprint.Footprint.RowPitch, static_cast<size_t>(m_readbackWidth) * 4);
    }
    const D3D12_RANGE noWrite = { 0, 0 };
    m_readbackBuffer->Unmap(0, &noWrite);
    m_readbackReady = true;
}

bool D3D12Device::TakeReadbackResult(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height)
{
    if (!m_readbackReady) return false;
    rgba.swap(m_readbackPixels);
    width = m_readbackWidth;
    height = m_readbackHeight;
    m_readbackPixels.clear();
    m_readbackReady = false;
    return true;
}

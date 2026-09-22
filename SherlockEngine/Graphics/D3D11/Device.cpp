#include "pch.h"
#include "Graphics/D3D11/Device.h"
#include "Graphics/D3D11/D3D11Convert.h"
#include "Core/Log.h"
#include <cstring>

namespace
{
    // 매 프레임 같은 오류를 찍지 않기 위한 1회 로그.
    void ErrorOnce(bool& flag, const char* message)
    {
        if (flag) return;
        flag = true;
        Log::Error("%s", message);
    }

    uint32_t BytesPerPixel(Format format)
    {
        switch (format)
        {
        case Format::R8G8B8A8_UNORM:
        case Format::R8G8B8A8_UNORM_SRGB:
        case Format::D24_UNORM_S8_UINT:
        case Format::D32_FLOAT:
        case Format::R32_FLOAT:
        case Format::R32_UINT:
            return 4;
        case Format::R16_UINT:            return 2;
        case Format::R32G32_FLOAT:        return 8;
        case Format::R32G32B32_FLOAT:     return 12;
        case Format::R32G32B32A32_FLOAT:  return 16;
        default:                          return 0;
        }
    }
}

// ------------------------------------------------------------------ 수명

bool Device::InitDevice(HWND hWnd, int width, int height)
{
    m_mainWindow = hWnd;
    m_screenWidth = width;
    m_screenHeight = height;

    if (!InitDirect3D()) return false;
    if (!CreateBackBufferTexture()) return false;
    if (!CreateDepthTexture()) return false;
    UpdateViewport();

    return true;
}

void Device::ReleaseDevice()
{
    if (m_context)
    {
        m_context->ClearState();
        m_context->Flush();
    }

    // 장치가 만든 객체를 먼저 놓는다. 장치보다 오래 살면 Live Object 경고가 난다.
    m_pipelines.Clear();
    m_shaders.Clear();
    m_textures.Clear();      // 백버퍼·깊이 버퍼 포함
    m_buffers.Clear();
    m_backBuffer = TextureHandle{};
    m_depthBuffer = TextureHandle{};

    m_swapChain.Reset();
    m_context.Reset();

#if defined(_DEBUG)
    // 장치만 남은 시점에 살아 있는 객체를 보고시킨다. 깨끗하면
    // "Live ID3D11Device ..." 한 줄만 나온다 (자식 객체 보고가 없어야 한다).
    if (m_device)
    {
        ComPtr<ID3D11Debug> debug;
        if (SUCCEEDED(m_device.As(&debug)))
        {
            debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
        }
        DumpDebugLayerMessages();
    }
#endif
    m_device.Reset();
}

void Device::DumpDebugLayerMessages()
{
#if defined(_DEBUG)
    if (!m_device) return;

    ComPtr<ID3D11InfoQueue> queue;
    if (FAILED(m_device.As(&queue))) return;   // Debug Layer가 없으면(SDK 미설치) 조용히 넘어간다.

    const UINT64 count = queue->GetNumStoredMessages();
    std::vector<char> buffer;
    for (UINT64 i = 0; i < count; ++i)
    {
        SIZE_T length = 0;
        if (FAILED(queue->GetMessage(i, nullptr, &length)) || length == 0) continue;
        buffer.resize(length);
        D3D11_MESSAGE* message = reinterpret_cast<D3D11_MESSAGE*>(buffer.data());
        if (FAILED(queue->GetMessage(i, message, &length))) continue;

        switch (message->Severity)
        {
        case D3D11_MESSAGE_SEVERITY_CORRUPTION:
        case D3D11_MESSAGE_SEVERITY_ERROR:
            Log::Error("D3D11: %.*s", static_cast<int>(message->DescriptionByteLength), message->pDescription);
            break;
        case D3D11_MESSAGE_SEVERITY_WARNING:
            Log::Warn("D3D11: %.*s", static_cast<int>(message->DescriptionByteLength), message->pDescription);
            break;
        default:
            Log::Info("D3D11: %.*s", static_cast<int>(message->DescriptionByteLength), message->pDescription);
            break;
        }
    }
    queue->ClearStoredMessages();
#endif
}

void Device::SetDebugName(ID3D11DeviceChild* object, const std::string& name, uint32_t index)
{
#if defined(_DEBUG)
    if (object == nullptr || name.empty()) return;
    std::string full = name;
    if (index > 0) full += "[" + std::to_string(index) + "]";
    object->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(full.size()), full.c_str());
#else
    (void)object; (void)name; (void)index;
#endif
}

// ------------------------------------------------------------------ 프레임

uint32_t Device::BeginFrame()
{
    m_frameIndex = static_cast<uint32_t>(m_frameCounter % kFrameCount);
    if (!m_context) return m_frameIndex;

    D3D11Texture* backBuffer = m_textures.Get(m_backBuffer);
    D3D11Texture* depthBuffer = m_textures.Get(m_depthBuffer);
    ID3D11RenderTargetView* rtv = backBuffer ? backBuffer->GetRTV(m_device.Get()) : nullptr;
    ID3D11DepthStencilView* dsv = depthBuffer ? depthBuffer->GetDSV(m_device.Get()) : nullptr;

    // 렌더 타깃은 PSO 밖의 상태다. 6단계에서 RenderPassDesc(타깃 + Load/Store)가 이 자리를 맡는다.
    m_context->OMSetRenderTargets(1, &rtv, dsv);
    if (rtv) m_context->ClearRenderTargetView(rtv, m_clearColor);
    if (dsv) m_context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    m_context->RSSetViewports(1, &m_viewport);

    return m_frameIndex;
}

void Device::EndFrame()
{
    if (m_swapChain)
    {
        // SyncInterval 1 = 수직 동기화(모니터 주사율로 제한), 0 = 제한 없음.
        m_swapChain->Present(m_vsync ? 1 : 0, 0);
    }
    // 이 프레임에 쌓인 Debug Layer 메시지를 콘솔로. 비어 있으면 비용이 거의 없다.
    DumpDebugLayerMessages();
    ++m_frameCounter;
}

void Device::SetClearColor(float r, float g, float b, float a)
{
    m_clearColor[0] = r; m_clearColor[1] = g; m_clearColor[2] = b; m_clearColor[3] = a;
}

void Device::Resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    if (!m_device || !m_context || !m_swapChain) return;

    m_screenWidth = width;
    m_screenHeight = height;

    // 순서가 중요하다.
    // (1) 렌더 타깃 바인딩 해제 → (2) 백버퍼·깊이 텍스처 파괴(스왑체인 버퍼 참조가 전부
    // 사라져야 ResizeBuffers가 DXGI_ERROR_INVALID_CALL 없이 성공한다) → (3) ResizeBuffers
    // → (4) 새 핸들로 재생성. 이전 핸들은 세대가 올라가 어디서 쓰든 nullptr가 된다.
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    DestroyTexture(m_backBuffer);
    DestroyTexture(m_depthBuffer);
    m_backBuffer = TextureHandle{};
    m_depthBuffer = TextureHandle{};

    const HRESULT hr = m_swapChain->ResizeBuffers(
        0,                       // 기존 BufferCount 유지
        m_screenWidth,
        m_screenHeight,
        DXGI_FORMAT_UNKNOWN,     // 기존 포맷 유지
        0);
    if (FAILED(hr))
    {
        Log::Error("ResizeBuffers() 실패. %s", Log::HrToString(hr).c_str());
        return;
    }

    if (!CreateBackBufferTexture())
    {
        Log::Error("Resize 후 백버퍼 재생성 실패.");
        return;
    }
    if (!CreateDepthTexture())
    {
        Log::Error("Resize 후 깊이 버퍼 재생성 실패.");
        return;
    }
    UpdateViewport();
}

// ------------------------------------------------------------------ 버퍼

BufferHandle Device::CreateBuffer(const BufferDesc& descIn, const void* initialData)
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

    D3D11Buffer buffer;
    buffer.desc = descIn;
    buffer.desc.debugName = nullptr;   // 포인터 수명을 믿지 않는다. 문자열로 복사.
    buffer.name = descIn.debugName ? descIn.debugName : "";

    // D3D11 상수 버퍼 크기는 16바이트 단위여야 한다. desc.size도 올림된 값을 보관해
    // UpdateBuffer의 크기 검사가 실제 버퍼와 맞게 한다.
    if (buffer.desc.bindFlags & BufferBind_Constant)
    {
        buffer.desc.size = (buffer.desc.size + 15) & ~15u;
    }

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = buffer.desc.size;
    if (buffer.desc.bindFlags & BufferBind_Vertex)         bd.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
    if (buffer.desc.bindFlags & BufferBind_Index)          bd.BindFlags |= D3D11_BIND_INDEX_BUFFER;
    if (buffer.desc.bindFlags & BufferBind_Constant)       bd.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
    if (buffer.desc.bindFlags & BufferBind_ShaderResource)
    {
        bd.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = buffer.desc.stride;
    }

    switch (buffer.desc.usage)
    {
    case BufferUsage::Default:
        bd.Usage = D3D11_USAGE_DEFAULT;
        break;
    case BufferUsage::Dynamic:
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        break;
    case BufferUsage::Staging:
        bd.Usage = D3D11_USAGE_STAGING;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        bd.BindFlags = 0;   // Staging은 파이프라인에 바인딩할 수 없다
        break;
    }

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = initialData;

    // Dynamic은 프레임마다 다른 복제본을 쓴다. 초기 데이터는 전부에 넣는다.
    const uint32_t copies = (buffer.desc.usage == BufferUsage::Dynamic) ? kFrameCount : 1;
    for (uint32_t i = 0; i < copies; ++i)
    {
        const HRESULT hr = m_device->CreateBuffer(&bd, initialData ? &init : nullptr, buffer.buffers[i].GetAddressOf());
        if (FAILED(hr))
        {
            Log::Error("CreateBuffer 실패 (%s, %u바이트). %s", buffer.name.c_str(), buffer.desc.size, Log::HrToString(hr).c_str());
            return BufferHandle{};
        }
        SetDebugName(buffer.buffers[i].Get(), buffer.name, i);
    }

    return m_buffers.Add(std::move(buffer));
}

void Device::DestroyBuffer(BufferHandle handle)
{
    m_buffers.Remove(handle);
}

void Device::UpdateBuffer(BufferHandle handle, const void* data, uint32_t size)
{
    static bool warnedInvalid = false;
    D3D11Buffer* buffer = m_buffers.Get(handle);
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

    ID3D11Buffer* target = buffer->Get(m_frameIndex);
    switch (buffer->desc.usage)
    {
    case BufferUsage::Dynamic:
    {
        // WRITE_DISCARD: 드라이버가 새 메모리를 주고 이전 내용은 버린다. GPU가 아직
        // 읽고 있어도 기다리지 않는다(이름 바꾸기, renaming). 한 프레임에 같은
        // 버퍼를 여러 번 Map해도 각 드로우는 자기 데이터를 본다. 대신 매번
        // 전체를 다시 써야 한다 — 이전 내용을 읽을 수 없다.
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const HRESULT hr = m_context->Map(target, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
        {
            Log::Error("UpdateBuffer : Map 실패 (%s). %s", buffer->name.c_str(), Log::HrToString(hr).c_str());
            return;
        }
        std::memcpy(mapped.pData, data, size);
        m_context->Unmap(target, 0);
        break;
    }
    case BufferUsage::Default:
        // UpdateSubresource는 pDstBox가 없으면 리소스 전체를 덮어쓴다. 부분 갱신은
        // 상수버퍼에서 허용되지 않으므로 전체 크기만 받는다.
        if (size != buffer->desc.size)
        {
            Log::Error("UpdateBuffer : Default 버퍼는 전체 크기로만 갱신할 수 있음 (%s, %u != %u).",
                buffer->name.c_str(), size, buffer->desc.size);
            return;
        }
        m_context->UpdateSubresource(target, 0, nullptr, data, 0, 0);
        break;
    case BufferUsage::Staging:
        Log::Error("UpdateBuffer : Staging 버퍼는 이 경로로 갱신하지 않음 (%s).", buffer->name.c_str());
        break;
    }
}

// ------------------------------------------------------------------ 텍스처

TextureHandle Device::CreateTexture(const TextureDesc& descIn, const void* initialData)
{
    if (!m_device)
    {
        Log::Error("CreateTexture : 장치가 없음.");
        return TextureHandle{};
    }

    D3D11Texture texture;
    texture.desc = descIn;
    texture.desc.debugName = nullptr;
    texture.name = descIn.debugName ? descIn.debugName : "";

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = descIn.width;
    td.Height = descIn.height;
    td.MipLevels = descIn.mipLevels;
    td.ArraySize = 1;
    td.Format = D3D11Convert::ToDXGI(descIn.format);
    td.SampleDesc.Count = descIn.sampleCount;
    td.SampleDesc.Quality = 0;
    td.Usage = D3D11_USAGE_DEFAULT;
    if (descIn.bindFlags & TextureBind_RenderTarget)   td.BindFlags |= D3D11_BIND_RENDER_TARGET;
    if (descIn.bindFlags & TextureBind_DepthStencil)   td.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    if (descIn.bindFlags & TextureBind_ShaderResource) td.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = initialData;
    init.SysMemPitch = descIn.width * BytesPerPixel(descIn.format);

    const HRESULT hr = m_device->CreateTexture2D(&td, initialData ? &init : nullptr, texture.texture.GetAddressOf());
    if (FAILED(hr))
    {
        Log::Error("CreateTexture2D 실패 (%s, %ux%u). %s", texture.name.c_str(), descIn.width, descIn.height, Log::HrToString(hr).c_str());
        return TextureHandle{};
    }
    SetDebugName(texture.texture.Get(), texture.name, 0);

    return m_textures.Add(std::move(texture));
}

void Device::DestroyTexture(TextureHandle handle)
{
    m_textures.Remove(handle);
}

bool Device::CreateBackBufferTexture()
{
    // 스왑체인이 이미 만든 텍스처를 감싼다. CreateTexture로는 만들 수 없는 유일한 텍스처.
    ComPtr<ID3D11Texture2D> backBuffer;
    const HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr))
    {
        Log::Error("SwapChain BackBuffer 가져오기 실패. %s", Log::HrToString(hr).c_str());
        return false;
    }

    D3D11Texture texture;
    texture.desc.width = m_screenWidth;
    texture.desc.height = m_screenHeight;
    texture.desc.format = Format::R8G8B8A8_UNORM;
    texture.desc.mipLevels = 1;
    texture.desc.bindFlags = TextureBind_RenderTarget;
    texture.desc.sampleCount = 1;
    texture.name = "BackBuffer";
    texture.texture = backBuffer;
    SetDebugName(texture.texture.Get(), texture.name, 0);

    m_backBuffer = m_textures.Add(std::move(texture));
    return m_backBuffer.IsValid();
}

bool Device::CreateDepthTexture()
{
    // 깊이·스텐실 텍스처. 크기와 샘플 수는 백버퍼와 일치해야 한다.
    TextureDesc desc;
    desc.width = m_screenWidth;
    desc.height = m_screenHeight;
    desc.format = Format::D24_UNORM_S8_UINT;
    desc.mipLevels = 1;
    desc.bindFlags = TextureBind_DepthStencil;
    desc.sampleCount = 1;   // 스왑체인과 동일 (MSAA 미사용)
    desc.debugName = "DepthBuffer";

    m_depthBuffer = CreateTexture(desc);
    return m_depthBuffer.IsValid();
}

// ------------------------------------------------------------------ 셰이더 / PSO

ShaderHandle Device::CreateShader(const ShaderDesc& desc)
{
    if (!m_device || desc.bytecode == nullptr || desc.bytecodeSize == 0)
    {
        Log::Error("CreateShader : 장치가 없거나 바이트코드가 비어 있음.");
        return ShaderHandle{};
    }

    D3D11Shader shader;
    shader.stage = desc.stage;
    shader.name = desc.debugName ? desc.debugName : "";

    HRESULT hr = S_OK;
    switch (desc.stage)
    {
    case ShaderStage::Vertex:
        hr = m_device->CreateVertexShader(desc.bytecode, desc.bytecodeSize, nullptr, shader.vs.GetAddressOf());
        SetDebugName(shader.vs.Get(), shader.name, 0);
        break;
    case ShaderStage::Pixel:
        hr = m_device->CreatePixelShader(desc.bytecode, desc.bytecodeSize, nullptr, shader.ps.GetAddressOf());
        SetDebugName(shader.ps.Get(), shader.name, 0);
        break;
    default:
        Log::Error("CreateShader : 알 수 없는 ShaderStage %d", static_cast<int>(desc.stage));
        return ShaderHandle{};
    }
    if (FAILED(hr))
    {
        Log::Error("CreateShader : 셰이더 객체 생성 실패 (%s). %s", shader.name.c_str(), Log::HrToString(hr).c_str());
        return ShaderHandle{};
    }

    // 입력 레이아웃 검증(VS)과 리플렉션용 사본.
    const uint8_t* bytes = static_cast<const uint8_t*>(desc.bytecode);
    shader.bytecode.assign(bytes, bytes + desc.bytecodeSize);

    return m_shaders.Add(std::move(shader));
}

void Device::DestroyShader(ShaderHandle handle)
{
    m_shaders.Remove(handle);
}

PipelineHandle Device::CreatePipeline(const PipelineStateDesc& desc)
{
    return m_pipelines.GetOrCreate(*this, desc);
}

// ------------------------------------------------------------------ 바인딩·드로우

void Device::BindPipeline(PipelineHandle handle)
{
    static bool warned = false;
    const PipelineState* state = m_pipelines.Get(handle);
    if (state == nullptr)
    {
        ErrorOnce(warned, "BindPipeline : 유효하지 않은 PipelineHandle.");
        return;
    }
    state->Bind(m_context.Get());
}

void Device::BindVertexBuffer(BufferHandle handle, uint32_t slot, uint32_t offset)
{
    static bool warned = false;
    const D3D11Buffer* buffer = m_buffers.Get(handle);
    if (buffer == nullptr)
    {
        ErrorOnce(warned, "BindVertexBuffer : 유효하지 않은 BufferHandle.");
        return;
    }
    ID3D11Buffer* d3dBuffer = buffer->Get(m_frameIndex);
    const UINT stride = buffer->desc.stride;
    m_context->IASetVertexBuffers(slot, 1, &d3dBuffer, &stride, &offset);
}

void Device::BindIndexBuffer(BufferHandle handle, Format indexFormat)
{
    static bool warned = false;
    const D3D11Buffer* buffer = m_buffers.Get(handle);
    if (buffer == nullptr)
    {
        ErrorOnce(warned, "BindIndexBuffer : 유효하지 않은 BufferHandle.");
        return;
    }
    m_context->IASetIndexBuffer(buffer->Get(m_frameIndex), D3D11Convert::ToDXGI(indexFormat), 0);
}

void Device::BindConstantBuffer(uint32_t slot, BufferHandle handle, uint8_t stageMask)
{
    static bool warned = false;
    const D3D11Buffer* buffer = m_buffers.Get(handle);
    if (buffer == nullptr)
    {
        ErrorOnce(warned, "BindConstantBuffer : 유효하지 않은 BufferHandle.");
        return;
    }
    ID3D11Buffer* d3dBuffer = buffer->Get(m_frameIndex);
    // D3D11은 스테이지마다 상수버퍼 슬롯이 따로 있다. PS가 b0을 읽으려면 PS에도 걸어야 한다.
    if (stageMask & ShaderStageMask_Vertex) m_context->VSSetConstantBuffers(slot, 1, &d3dBuffer);
    if (stageMask & ShaderStageMask_Pixel)  m_context->PSSetConstantBuffers(slot, 1, &d3dBuffer);
}

void Device::DrawIndexed(uint32_t indexCount, uint32_t startIndex, int32_t baseVertex)
{
    m_context->DrawIndexed(indexCount, startIndex, baseVertex);
}

// ------------------------------------------------------------------ 내부

bool Device::InitDirect3D()
{
    const D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_HARDWARE;

    UINT createDeviceFlags = 0;

#if defined(DEBUG) || defined(_DEBUG)
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

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
        Log::Error("D3D11CreateDeviceAndSwapChain() 실패. %s", Log::HrToString(hr).c_str());
        return false;
    }

    if (featureLevel != D3D_FEATURE_LEVEL_11_0)
    {
        Log::Error("D3D Feature Level 11_0 지원 안 함.");
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
        Log::Warn("4X MSAA 지원 안 됨.");
    }
    else
    {
        Log::Info("4X MSAA 지원됨. QualityLevels: %u", m_numQualityLevels);
    }
    return true;
}

void Device::UpdateViewport()
{
    // 뷰포트는 동적 상태다. BeginFrame이 매 프레임 건다.
    ZeroMemory(&m_viewport, sizeof(D3D11_VIEWPORT));
    m_viewport.TopLeftX = 0.0f;
    m_viewport.TopLeftY = 0.0f;
    m_viewport.Width = static_cast<float>(m_screenWidth);
    m_viewport.Height = static_cast<float>(m_screenHeight);
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;
}

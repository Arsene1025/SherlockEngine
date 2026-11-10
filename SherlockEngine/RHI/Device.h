#pragma once
#include <cstdint>
#include <cstddef>
#include <span>     // C++20: 배열 인자는 (포인터, 개수) 대신 std::span 으로 받음
#include <vector>
#include "RHI/Handle.h"
#include "RHI/PipelineTypes.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "RHI/RenderPassTypes.h"
#include "RHI/CommandList.h"

// RHI::Device — 리소스 생성과 프레임 경계. 리소스는 핸들이고 Device/CommandList 만 가상 인터페이스임
// (rendering-analysis D21).
//
// 규칙:
//   - 이 헤더와 RHI/ 의 다른 헤더는 D3D 헤더를 include 하지 않음. 구현은 RHI/D3D11/ 과 RHI/D3D12/ 에만.
//   - 호출 코드(Graphics/Renderer, App)는 핸들과 Desc 만 본다. 백엔드 클래스 이름을 알 필요가 없음.
//   - 백엔드 선택은 RHI/RHI.h 의 CreateDevice(Backend, DeviceDesc) 팩토리.
//
// D3D11 에서만 편한 것(즉시 Map, 자동 밉 생성, 암묵적 동기화)은 인터페이스에 넣지 않았음.
// UpdateBuffer 는 D3D12 에서도 지킬 수 있는 의미, 즉 "이 프레임 슬롯의 내용을 통째로 교체"만 약속함.
namespace RHI
{
	enum class Backend : uint8_t
	{
		D3D11,
		D3D12,

	};

	struct DeviceDesc
	{
		void* windowHandle = nullptr;   // HWND. RHI 헤더에 windows.h 를 끌어오지 않으려고 void* 로 둠
		int width = 0;
		int height = 0;
		bool enableDebugLayer = true;   // Debug 빌드에서만 실제로 켜짐
	};

	class Device
	{
	public:
		virtual ~Device() = default;

		virtual Backend GetBackend() const = 0;
		virtual const char* GetBackendName() const = 0;
		virtual bool IsInitialized() const = 0;

		// ---- 프레임 ----
		// 프레임 인덱스를 갱신하고 돌려줌. Dynamic 버퍼는 이 인덱스의 복제본을 씀 (GraphicsConfig.h).
		// 타깃 바인딩·클리어는 하지 않음. 그 일은 CommandList::BeginRenderPass 가 맡음.
		// 프레임 구조: BeginFrame → [ShadowPass → MainPass → UIPass] → EndFrame(Present).
		virtual uint32_t BeginFrame() = 0;
		virtual void EndFrame() = 0;   // Present. 열린 렌더 패스가 있으면 에러 로그를 남기고 닫음.
		virtual uint32_t GetFrameIndex() const = 0;
		virtual CommandList& GetCommandList() = 0;

		// 가장 최근에 완료된 프레임의 타임스탬프. ticks[i] = 그 프레임에서 slot i 에 기록한 값 (기록 안 했으면 0),
		// frequency = 초당 틱. 아직 어떤 프레임도 끝나지 않았으면 false. 호출해도 CPU 가 대기하지 않음
		// (D3D12: 펜스가 이미 지난 슬롯, D3D11: 2프레임 전 쿼리). C++20: (uint64_t*, count) → std::span.
		// 호출자의 C 배열이 크기와 함께 넘어오므로 count 를 따로 적을 일이 없음. ticks.size() 가
		// kMaxTimestamps 보다 크면 kMaxTimestamps 개까지만 채움.
		virtual bool GetTimestampResults(std::span<uint64_t> ticks, uint64_t& frequency, uint64_t& frameNumber) = 0;

		// 다음 EndFrame 에서 Present 직전의 백버퍼를 CPU 로 읽음 (스크린샷). 동기 방식: 그 프레임의 EndFrame
		// 이 GPU 를 기다림. TakeReadbackResult 는 준비된 결과(RGBA8, 위→아래, 행 간격 = width*4)를 한 번
		// 돌려주고 비움.
		virtual void RequestBackBufferReadback() = 0;
		virtual bool TakeReadbackResult(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height) = 0;

		virtual void Resize(int width, int height) = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
		virtual bool IsTearingSupported() const = 0;

		// ---- 리소스: Desc → 핸들 ----
		virtual BufferHandle CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;
		virtual void DestroyBuffer(BufferHandle handle) = 0;
		// Dynamic: 이 프레임 슬롯의 내용을 통째로 교체 (size ≤ desc.size). Default: 전체 크기로만 갱신.
		virtual void UpdateBuffer(BufferHandle handle, const void* data, uint32_t size) = 0;

		// subresources 는 서브리소스마다 하나(GetArraySize(desc) × desc.mipLevels 개, 면 우선 순서 —
		// ResourceDesc.h). 2D 면 밉 레벨마다 하나. 비어 있으면(기본값) 빈 텍스처. desc.dimension =
		// TextureCube 는 정사각형 ShaderResource 전용. DepthStencil | ShaderResource 조합(그림자 맵·씬 깊이)은
		// 백엔드가 TYPELESS + 포맷별 뷰로 만듦. C++20: (포인터, 개수) → std::span.
		// std::vector<TextureSubresource> 를 그대로 넘김.
		virtual TextureHandle CreateTexture(const TextureDesc& desc, std::span<const TextureSubresource> subresources = {}) = 0;
		virtual void DestroyTexture(TextureHandle handle) = 0;
		// Resize 할 때마다 새로 만들어짐. 프레임을 넘겨 보관하지 말 것.
		virtual TextureHandle GetBackBuffer() const = 0;
		// 깊이 버퍼는 장치가 갖지 않음. 그리는 쪽(Renderer)이 출력 크기에 맞춰 만들고 소유함 — 에디터의 씬
		// 뷰와 게임 모드가 같은 경로를 타고, 후처리(SSAO·디퍼드)가 깊이를 SRV 로 읽을 수 있음.

		// ---- 초기 데이터 업로드 묶기 ----
		// Begin 과 End 사이에 초기 데이터와 함께 만든 버퍼·텍스처의 GPU 복사를 한 번에 제출함. D3D12 는 리소스마다
		// "제출 → GPU 대기"를 하지 않고 End 에서 한 번만 기다림 (묶지 않으면 CreateBuffer/CreateTexture 가 즉시
		// 동기 업로드함). D3D11 은 생성 시 드라이버가 복사하므로 아무 일도 하지 않음. 중첩 가능 (가장 바깥 End
		// 에서 제출). 묶음 안에서 만든 리소스는 End 뒤에 그리는 명령에서만 쓸 것 — 같은 프레임의 드로우는 EndFrame
		// 에서 제출되므로 Render 안에서 묶는 것은 안전함.
		virtual void BeginResourceUploads() = 0;
		virtual void EndResourceUploads() = 0;

		virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
		virtual void DestroySampler(SamplerHandle handle) = 0;

		virtual ShaderHandle CreateShader(const ShaderDesc& desc) = 0;   // 바이트코드는 백엔드별 (DXBC / DXIL)
		virtual void DestroyShader(ShaderHandle handle) = 0;

		virtual PipelineHandle CreatePipeline(const PipelineStateDesc& desc) = 0;   // 같은 Desc 면 같은 핸들을 돌려줌(캐시)
		virtual size_t GetPipelineCount() const = 0;
		virtual void InvalidatePipelines() = 0;   // 캐시를 전부 버림. 이전 핸들은 무효가 됨. 셰이더 핫리로드 뒤에 호출.

		virtual BindingLayoutHandle CreateBindingLayout(const BindingLayoutDesc& desc) = 0;
		virtual void DestroyBindingLayout(BindingLayoutHandle handle) = 0;
		virtual ResourceSetHandle CreateResourceSet(const ResourceSetDesc& desc) = 0;
		virtual void DestroyResourceSet(ResourceSetHandle handle) = 0;

		// ---- ImGui 렌더러 어댑터 ----
		// ImGui 렌더러 구현(DX11/DX12)은 그래픽스 API 마다 따로 있으므로 어느 것을 쓸지는 Device 가 정함
		// (얇은 어댑터 방식). App 은 ImGui 컨텍스트와 Win32 백엔드만 다룸.
		// GetImGuiTextureId 의 반환값은 ImTextureID(ImU64) 로 캐스팅해 ImGui::Image 에 넘김. 유효하지 않으면 0.
		virtual bool InitImGui() = 0;
		virtual void NewFrameImGui() = 0;
		virtual void RenderImGui() = 0;      // Renderer::BeginUIPass / EndUIPass 사이에서 호출
		virtual void ShutdownImGui() = 0;
		virtual uint64_t GetImGuiTextureId(TextureHandle texture) = 0;
	};
}

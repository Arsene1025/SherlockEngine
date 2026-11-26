#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/ShaderCompiler.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "Graphics/ShaderConstants.h"
#include "UI/UIDrawList.h"
#include "Graphics/VertexTypes.h"
#include "Graphics/Frustum.h"
#include "Graphics/DebugDraw.h"
#include "Graphics/Mesh.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Scene/Highlight.h"
#include "Scene/Dissolve.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include "Core/Profiler.h"   // GPU 구간
#include <algorithm>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

// ------------------------------------------------------------------ 셰이더·패스 표
//
// 셰이더를 추가하는 절차: Renderer.h 의 ShaderId 에 한 줄 → 아래 kShaders 에 한 줄 → (새 패스면) PassId 와
// kPasses 에 한 줄 → BuildPassDescs 에 그 패스의 래스터라이저·깊이 설정. 로드, 핫리로드 감시, cbuffer 크기
// 대조, 바인딩 대조, 해제는 전부 이 표를 돌기 때문에 따로 손댈 곳이 없음.

struct Renderer::ShaderInfo
{
	const wchar_t* file;   // Shaders/ 의 파일 이름
	const char* entry;     // 진입점
	ShaderStage stage;
	const char* name;   // 디버그 이름 (로그, Debug Layer)
};

const Renderer::ShaderInfo Renderer::kShaders[Renderer::Shader_Count] = {
	{ L"BasicVertexShader.hlsl", "VS_Main", ShaderStage::Vertex, "BasicVS" },
	{ L"BasicPixelShader.hlsl", "PS_Main", ShaderStage::Pixel, "BasicPS" },
	{ L"ShadowVertexShader.hlsl", "VS_Shadow", ShaderStage::Vertex, "ShadowVS" },
	{ L"SkyboxVertexShader.hlsl", "VS_Sky", ShaderStage::Vertex, "SkyboxVS" },
	{ L"SkyboxPixelShader.hlsl", "PS_Sky", ShaderStage::Pixel, "SkyboxPS" },
	// 화면을 덮는 삼각형 하나를 그림. 톤매핑·블러·블룸의 실제 처리는 각 PS 가 함.
	{ L"ScreenVertexShader.hlsl", "VS_Screen", ShaderStage::Vertex, "ScreenVS" },
	{ L"TonemapPixelShader.hlsl", "PS_Tonemap", ShaderStage::Pixel, "ToneMapPS" },
	{ L"PostPixelShader.hlsl", "PS_Post", ShaderStage::Pixel, "PostPS" },                   // 블러·블룸
	{ L"BasicSkinVertexShader.hlsl", "VS_MainSkin", ShaderStage::Vertex, "BasicSkinVS" },   // 스키닝 메시. PS 는 BasicPS 를 같이 씀
	{ L"ShadowSkinVertexShader.hlsl", "VS_ShadowSkin", ShaderStage::Vertex, "ShadowSkinVS" },
	{ L"DebugLineVertexShader.hlsl", "VS_Line", ShaderStage::Vertex, "DebugLineVS" },   // 본 표시 등 디버그 선
	{ L"DebugLinePixelShader.hlsl", "PS_Line", ShaderStage::Pixel, "DebugLinePS" },
	{ L"BlurComputeShader.hlsl", "CS_Blur", ShaderStage::Compute, "BlurCS" },                                       // 가우시안 블러 한 방향 (컴퓨트 경로)
	{ L"BlurWeightsComputeShader.hlsl", "CS_BlurWeights", ShaderStage::Compute, "BlurWeightsCS" },                  // 블러 가중치 → 구조체 버퍼 (u0)
	{ L"ParticleResetComputeShader.hlsl", "CS_ParticleReset", ShaderStage::Compute, "ParticleResetCS" },            // 새 버퍼: 전부 죽음, 사망 목록 = 모든 슬롯
	{ L"ParticleSimComputeShader.hlsl", "CS_ParticleSim", ShaderStage::Compute, "ParticleSimCS" },                  // 생존 목록 적분 + (켜져 있으면) 화면 공간 충돌 (간접 디스패치)
	{ L"ParticleArgsComputeShader.hlsl", "CS_ParticleArgs", ShaderStage::Compute, "ParticleArgsCS" },               // 생성 수 확정, 간접 인자
	{ L"ParticleEmitComputeShader.hlsl", "CS_ParticleEmit", ShaderStage::Compute, "ParticleEmitCS" },               // 사망 목록 → 새 파티클
	{ L"ParticleSortInitComputeShader.hlsl", "CS_ParticleSortInit", ShaderStage::Compute, "ParticleSortInitCS" },   // 알파: 생존 목록 → (뷰 z, 번호)
	{ L"ParticleSortStepComputeShader.hlsl", "CS_ParticleSortStep", ShaderStage::Compute, "ParticleSortStepCS" },   // 알파: 바이토닉 한 단계
	{ L"ParticleVertexShader.hlsl", "VS_Particle", ShaderStage::Vertex, "ParticleVS" },                             // 파티클 빌보드 (t21 + t22 생존 목록 또는 알파 이미터의 정렬된 번호, SV_InstanceID)
	{ L"ParticlePixelShader.hlsl", "PS_Particle", ShaderStage::Pixel, "ParticlePS" },
	// 디퍼드·SSAO (docs/plan/deferred-clustered-ssao-record.html)
	{ L"GBufferPixelShader.hlsl", "PS_GBuffer", ShaderStage::Pixel, "GBufferPS" },                           // 재질 → MRT 6장 (발광 hdr + G-buffer 넷 + 속도. VS 는 BasicVS/BasicSkinVS)
	{ L"DeferredLightingPixelShader.hlsl", "PS_DeferredLighting", ShaderStage::Pixel, "DeferredLightPS" },   // 풀스크린: G-buffer → 조명 (가산)
	{ L"SsaoPixelShader.hlsl", "PS_Ssao", ShaderStage::Pixel, "SsaoPS" },                                    // 풀스크린: 깊이·노멀 → 가림 (R8)
	{ L"SsaoBlurPixelShader.hlsl", "PS_SsaoBlur", ShaderStage::Pixel, "SsaoBlurPS" },                        // 4×4 깊이 가중 블러
	// 속도 버퍼·모션 블러·TAA (docs/plan/velocity-motionblur-taa-plan.html)
	{ L"VelocityDebugPixelShader.hlsl", "PS_VelocityDebug", ShaderStage::Pixel, "VelocityDebugPS" },   // 디버그 뷰 16(속도)·17(속도 오차) → hdr
	{ L"MotionBlurPixelShader.hlsl", "PS_MotionBlur", ShaderStage::Pixel, "MotionBlurPS" },            // 속도를 따라 번짐: hdr → 스크래치
	{ L"TaaPixelShader.hlsl", "PS_Taa", ShaderStage::Pixel, "TaaPS" },                                 // TAA 리졸브: hdr + 히스토리 → 히스토리
	// GPU 클러스터 비닝 (docs/plan/gpu-cluster-binning-record.html, Shaders/ClusterBin.hlsli)
	{ L"ClusterViewComputeShader.hlsl", "CS_ClusterView", ShaderStage::Compute, "ClusterViewCS" },      // 경계 구 → 뷰 공간
	{ L"ClusterTileComputeShader.hlsl", "CS_ClusterTile", ShaderStage::Compute, "ClusterTileCS" },      // 1단계: 타일 144 개의 조명 비트마스크
	{ L"ClusterCountComputeShader.hlsl", "CS_ClusterCount", ShaderStage::Compute, "ClusterCountCS" },   // 2단계: 상자마다 개수
	{ L"ClusterScanComputeShader.hlsl", "CS_ClusterScan", ShaderStage::Compute, "ClusterScanCS" },      // 누적합 → (시작, 개수)
	{ L"ClusterFillComputeShader.hlsl", "CS_ClusterFill", ShaderStage::Compute, "ClusterFillCS" },      // 2단계: 번호 채우기
	// Gerstner 바다 (docs/plan/ocean-gerstner-plan.html). PS 는 BasicPS·GBufferPS 에 픽셀 단위 노멀만 바꾼 것 (3단계)
	{ L"OceanVertexShader.hlsl", "VS_Ocean", ShaderStage::Vertex, "OceanVS" },
	{ L"OceanPixelShader.hlsl", "PS_Ocean", ShaderStage::Pixel, "OceanPS" },
	{ L"OceanGBufferPixelShader.hlsl", "PS_OceanGBuffer", ShaderStage::Pixel, "OceanGBufferPS" },
	// 게임 UI 캔버스 (docs/plan/ui-canvas-plan.html, Shaders/UI.hlsli)
	{ L"UIVertexShader.hlsl", "VS_UI", ShaderStage::Vertex, "UIVS" },
	{ L"UIPixelShader.hlsl", "PS_UI", ShaderStage::Pixel, "UIPS" },
	// 알파 테스트 그림자 (컷아웃 재질·디졸브. Shaders/ShadowAlpha.hlsli)
	{ L"ShadowAlphaVertexShader.hlsl", "VS_ShadowAlpha", ShaderStage::Vertex, "ShadowAlphaVS" },
	{ L"ShadowAlphaSkinVertexShader.hlsl", "VS_ShadowAlphaSkin", ShaderStage::Vertex, "ShadowAlphaSkinVS" },
	{ L"ShadowAlphaPixelShader.hlsl", "PS_ShadowAlpha", ShaderStage::Pixel, "ShadowAlphaPS" },
	// 비 (docs/plan/rain-plan.html, Shaders/Rain.hlsli). 정점 버퍼 없이 SV_VertexID 로 빗방울·튐 위치를 계산 (상태 없는 절차적 파티클)
	{ L"RainVertexShader.hlsl", "VS_Rain", ShaderStage::Vertex, "RainVS" },
	{ L"RainPixelShader.hlsl", "PS_Rain", ShaderStage::Pixel, "RainPS" },
	{ L"RainSplashVertexShader.hlsl", "VS_RainSplash", ShaderStage::Vertex, "RainSplashVS" },
	{ L"RainSplashPixelShader.hlsl", "PS_RainSplash", ShaderStage::Pixel, "RainSplashPS" },
};

struct Renderer::PassInfo
{
	const char* name;
	uint32_t vs;                         // ShaderId
	uint32_t ps;                         // ShaderId. Shader_Count = 픽셀 셰이더 없음 (깊이 전용)
	uint32_t layouts[kMaxBindingSets];   // LayoutId. 순서가 곧 셋 인덱스 (kSetFrame 등)
	uint32_t layoutCount;
};

const Renderer::PassInfo Renderer::kPasses[Renderer::Pass_Count] = {
	{ "Main", Shader_BasicVS, Shader_BasicPS, { Layout_Frame, Layout_Object, Layout_Material }, 3 },
	{ "Shadow", Shader_ShadowVS, Shader_Count, { Layout_Frame, Layout_Object }, 2 },
	{ "Sky", Shader_SkyVS, Shader_SkyPS, { Layout_Frame }, 1 },
	{ "Tonemap", Shader_ScreenVS, Shader_TonemapPS, { Layout_Post }, 1 },
	{ "Post", Shader_ScreenVS, Shader_PostPS, { Layout_Post }, 1 },
	{ "MainSkin", Shader_BasicSkinVS, Shader_BasicPS, { Layout_Frame, Layout_Object, Layout_Material }, 3 },
	{ "ShadowSkin", Shader_ShadowSkinVS, Shader_Count, { Layout_Frame, Layout_Object }, 2 },
	{ "DebugLine", Shader_DebugLineVS, Shader_DebugLinePS, { Layout_Frame }, 1 },
	{ "Particle", Shader_ParticleVS, Shader_ParticlePS, { Layout_Frame, Layout_ParticleDraw, Layout_ParticleDepth }, 3 },
	{ "GBuffer", Shader_BasicVS, Shader_GBufferPS, { Layout_Frame, Layout_Object, Layout_Material }, 3 },   // 메인과 같은 셋 — 같은 드로우 루프가 그림
	{ "GBufferSkin", Shader_BasicSkinVS, Shader_GBufferPS, { Layout_Frame, Layout_Object, Layout_Material }, 3 },
	{ "DeferredLight", Shader_ScreenVS, Shader_DeferredLightPS, { Layout_Frame, Layout_GBuffer }, 2 },
	{ "Ssao", Shader_ScreenVS, Shader_SsaoPS, { Layout_Ssao }, 1 },
	{ "SsaoBlur", Shader_ScreenVS, Shader_SsaoBlurPS, { Layout_Ssao }, 1 },
	{ "VelocityDebug", Shader_ScreenVS, Shader_VelocityDebugPS, { Layout_Temporal }, 1 },
	{ "MotionBlur", Shader_ScreenVS, Shader_MotionBlurPS, { Layout_Temporal }, 1 },
	{ "Taa", Shader_ScreenVS, Shader_TaaPS, { Layout_Temporal }, 1 },
	{ "Ocean", Shader_OceanVS, Shader_OceanPS, { Layout_Frame, Layout_Object, Layout_Material }, 3 },   // 메인과 같은 셋 — 같은 드로우 루프가 그림
	{ "GBufferOcean", Shader_OceanVS, Shader_OceanGBufferPS, { Layout_Frame, Layout_Object, Layout_Material }, 3 },
	{ "UI", Shader_UIVS, Shader_UIPS, { Layout_UICommon, Layout_UITexture }, 2 },   // 톤 패스 뒤, UNORM 뷰 (RendererUI.cpp)
	{ "UIWorld", Shader_UIVS, Shader_UIPS, { Layout_UICommon, Layout_UITexture }, 2 },   // World 캔버스: 같은 셰이더 + 읽기 전용 깊이
	{ "ShadowAlpha", Shader_ShadowAlphaVS, Shader_ShadowAlphaPS, { Layout_Frame, Layout_Object, Layout_Material }, 3 },   // 그림자 + 알파 테스트 (재질 셋의 알베도 알파·노이즈)
	{ "ShadowAlphaSkin", Shader_ShadowAlphaSkinVS, Shader_ShadowAlphaPS, { Layout_Frame, Layout_Object, Layout_Material }, 3 },
	{ "Rain", Shader_RainVS, Shader_RainPS, { Layout_Frame, Layout_Rain, Layout_ParticleDepth }, 3 },   // 셋 번호가 파티클 패스와 같음 (kSetParticle·kSetParticleDepth)
	{ "RainSplash", Shader_RainSplashVS, Shader_RainSplashPS, { Layout_Frame, Layout_Rain, Layout_ParticleDepth }, 3 },
};

struct Renderer::ComputePassInfo
{
	const char* name;
	uint32_t cs;                         // ShaderId
	uint32_t layouts[kMaxBindingSets];   // LayoutId. 순서가 곧 셋 인덱스 (SetComputeResourceSet 의 첫 인자)
	uint32_t layoutCount;
};

const Renderer::ComputePassInfo Renderer::kComputePasses[Renderer::ComputePass_Count] = {
	{ "BlurCompute", Shader_BlurCS, { Layout_BlurCompute }, 1 },
	{ "BlurWeights", Shader_BlurWeightsCS, { Layout_BlurWeights }, 1 },
	{ "ParticleReset", Shader_ParticleResetCS, { Layout_ParticleSim }, 1 },                        // 셰이더마다 쓰는 슬롯만 다름
	{ "ParticleSim", Shader_ParticleSimCS, { Layout_ParticleStep, Layout_ParticleCollide }, 2 },   // 인자 버퍼를 바인딩하지 않는 레이아웃 (아래 Layout_ParticleStep)
	{ "ParticleArgs", Shader_ParticleArgsCS, { Layout_ParticleSim }, 1 },
	{ "ParticleEmit", Shader_ParticleEmitCS, { Layout_ParticleSim }, 1 },
	{ "ParticleSortInit", Shader_ParticleSortInitCS, { Layout_ParticleSort, Layout_ParticleSortStep }, 2 },
	{ "ParticleSortStep", Shader_ParticleSortStepCS, { Layout_ParticleSort, Layout_ParticleSortStep }, 2 },
	{ "ClusterView", Shader_ClusterViewCS, { Layout_ClusterBin }, 1 },   // 다섯 패스가 같은 셋 하나 (셰이더마다 쓰는 슬롯만 다름)
	{ "ClusterTile", Shader_ClusterTileCS, { Layout_ClusterBin }, 1 },
	{ "ClusterCount", Shader_ClusterCountCS, { Layout_ClusterBin }, 1 },
	{ "ClusterScan", Shader_ClusterScanCS, { Layout_ClusterBin }, 1 },
	{ "ClusterFill", Shader_ClusterFillCS, { Layout_ClusterBin }, 1 },
};

namespace
{
	constexpr float kHotReloadPollInterval = 0.5f;   // 초

	// 오브젝트에 켜진 T(Highlight·Dissolve)가 있으면 그것, 없으면 조상에게서 물려받은 것 (가까운 쪽이 이김).
	template <typename T>
	const T* ResolveInherited(const GameObject& object, const T* inherited)
	{
		for (const auto& behaviour : object.GetBehaviours())
		{
			const T* own = dynamic_cast<const T*>(behaviour.get());
			if (own != nullptr && own->enabled) return own;
		}
		return inherited;
	}
	constexpr uint32_t kInitialLightCapacity = 64;   // 조명 구조체 버퍼의 처음 원소 수 (5,120바이트)

	// HLSL cbuffer 이름 ↔ C++ 구조체 크기. 셰이더가 선언한 cbuffer 는 전부 여기 있어야 함 (없으면 로드 실패).
	struct ConstantBufferInfo
	{
		const char* name;
		uint32_t size;
	};
	constexpr ConstantBufferInfo kConstantBuffers[] = {
		{ "PerFrame", sizeof(PerFrameConstants) },
		{ "PerObject", sizeof(PerObjectConstants) },
		{ "Lights", sizeof(LightConstants) },
		{ "Material", sizeof(MaterialConstants) },
		{ "PostParams", sizeof(PostParamsConstants) },
		{ "ShadowPass", sizeof(ShadowPassConstants) },
		{ "Shadows", sizeof(ShadowConstants) },
		{ "Skin", sizeof(SkinConstants) },
		{ "ParticleEmitter", sizeof(ParticleEmitterConstants) },
		{ "ParticleCollision", sizeof(ParticleCollisionConstants) },
		{ "ParticleSortView", sizeof(ParticleSortViewConstants) },
		{ "ParticleSortStep", sizeof(ParticleSortStepConstants) },
		{ "SsaoParams", sizeof(SsaoConstants) },
		{ "PrevSkin", sizeof(PrevSkinConstants) },
		{ "ClusterBin", sizeof(ClusterBinConstants) },
		{ "Ocean", sizeof(OceanConstants) },
		{ "UIConstants", sizeof(UIConstants) },
		{ "Rain", sizeof(RainConstants) },
	};

	// 레지스터 문자. 텍스처와 구조체 버퍼는 같은 t# 공간을 씀.
	char RegisterClassOf(BindingType type)
	{
		switch (type)
		{
		case BindingType::ConstantBuffer:           return 'b';
		case BindingType::Sampler:                  return 's';
		case BindingType::UnorderedAccess:          return 'u';
		case BindingType::BufferUnorderedAccess:    return 'u';
		case BindingType::RawBufferUnorderedAccess: return 'u';
		default:                                    return 't';
		}
	}
}

Renderer::Renderer()
{
	m_defaultMaterial.name = "Default";
}

Renderer::~Renderer()
{
	Shutdown();
}

bool Renderer::Initialize(RHI::Device* device)
{
	if (device == nullptr)
	{
		Log::Error("Renderer::Initialize : Device가 없음.");
		return false;
	}
	m_device = device;

	// ---- 상수버퍼. 전부 Dynamic(매 프레임/드로우마다 Map WRITE_DISCARD) ----
	// C++20: Desc 는 지정 초기화(designated initializer)로 채움. "빈 Desc 를 만들고 필드를 하나씩 대입"하던
	// 코드가 D3D12 의 desc 채우기와 같은 한 덩어리 리터럴이 됨. 나열하지 않은 필드(stride)는 헤더의 기본값을 받음.
	auto makeCB = [this](uint32_t size, const char* name)
	{
		const BufferDesc desc{ .size = size, .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_Constant, .debugName = name };
		return m_device->CreateBuffer(desc);
	};
	m_perFrameCB = makeCB(sizeof(PerFrameConstants), "CB_PerFrame");
	m_perObjectCB = makeCB(sizeof(PerObjectConstants), "CB_PerObject");
	m_skinCB = makeCB(sizeof(SkinConstants), "CB_Skin");               // b7
	m_prevSkinCB = makeCB(sizeof(PrevSkinConstants), "CB_PrevSkin");   // b13 (속도 버퍼: 지난 프레임 팔레트)
	m_oceanCB = makeCB(sizeof(OceanConstants), "CB_Ocean");            // b9 (바다 파도, 프레임에 한 번)
	m_rainCB = makeCB(sizeof(RainConstants), "CB_Rain");               // b8 비 셋 (빗줄기·튐, 비가 있는 프레임에 한 번)
	m_skinScratch.resize(kMaxSkinJoints);
	m_debugLineVB = m_device->CreateBuffer(BufferDesc{ .size = DebugDraw::kMaxVertices * static_cast<uint32_t>(sizeof(DebugLineVertex)), .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_Vertex, .stride = sizeof(DebugLineVertex), .debugName = "VB_DebugLines" });
	m_lightCB = makeCB(sizeof(LightConstants), "CB_Lights");
	// 조명 배열은 상수버퍼가 아니라 구조체 버퍼 (t15). 처음에는 kInitialLightCapacity 개 자리, 모자라면
	// UploadLights 가 두 배로 다시 만듦.
	m_lightCapacity = kInitialLightCapacity;
	m_lightBuffer = m_device->CreateBuffer(BufferDesc{ .size = m_lightCapacity * static_cast<uint32_t>(sizeof(GpuLight)), .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_ShaderResource, .stride = sizeof(GpuLight), .debugName = "SB_Lights" });
	m_postParamsCB = makeCB(sizeof(PostParamsConstants), "CB_PostParams");
	// 블러 가중치: GPU 가 쓰고 GPU 가 읽는 구조체 버퍼 (Default + UAV + SRV). 초기 데이터 없음 — 블러 전에 항상 씀.
	m_blurWeights = m_device->CreateBuffer(BufferDesc{ .size = (kMaxBlurRadius + 1) * static_cast<uint32_t>(sizeof(float)), .usage = BufferUsage::Default, .bindFlags = BufferBind_ShaderResource | BufferBind_UnorderedAccess, .stride = sizeof(float), .debugName = "SB_BlurWeights" });
	m_shadowPassCB = makeCB(sizeof(ShadowPassConstants), "CB_ShadowPass");   // b5
	m_shadowsCB = makeCB(sizeof(ShadowConstants), "CB_Shadows");             // b6
	m_ssaoCB = makeCB(sizeof(SsaoConstants), "CB_Ssao");                     // b12 (SSAO 셋)
	m_uiCB = makeCB(sizeof(UIConstants), "CB_UI");                           // b4 (게임 UI 셋. 캔버스 행렬, 프레임에 한 번)
	// 클러스터 (3b): 상자마다 (시작, 개수) 와 조명 번호 목록. 둘 다 프레임 셋(t30, t31)에 꽂히고 뷰마다 다시 올림 (Dynamic).
	// 번호 목록은 모자라면 EnsureClusterCapacity 가 두 배로 다시 만듦.
	m_clusterGridBuffer = m_device->CreateBuffer(BufferDesc{ .size = kClusterCount * 2 * static_cast<uint32_t>(sizeof(uint32_t)), .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_ShaderResource, .stride = 2 * sizeof(uint32_t), .debugName = "SB_ClusterGrid" });
	m_clusterIndexCapacity = kInitialClusterLightRefs;
	m_clusterIndexBuffer = m_device->CreateBuffer(BufferDesc{ .size = m_clusterIndexCapacity * static_cast<uint32_t>(sizeof(uint32_t)), .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_ShaderResource, .stride = sizeof(uint32_t), .debugName = "SB_ClusterLights" });
	if (!m_perFrameCB.IsValid() || !m_perObjectCB.IsValid() || !m_skinCB.IsValid() || !m_prevSkinCB.IsValid() || !m_oceanCB.IsValid() || !m_debugLineVB.IsValid() || !m_lightCB.IsValid() || !m_lightBuffer.IsValid() || !m_postParamsCB.IsValid() ||
		!m_shadowPassCB.IsValid() || !m_shadowsCB.IsValid() || !m_blurWeights.IsValid() || !m_ssaoCB.IsValid() || !m_clusterGridBuffer.IsValid() || !m_clusterIndexBuffer.IsValid() || !m_uiCB.IsValid() || !m_rainCB.IsValid())
	{
		Log::Error("Renderer::Initialize : 상수버퍼 생성 실패.");
		return false;
	}

	if (!CreateSamplers() || !m_textures.Initialize(m_device) || !CreateShadowResources() || !CreateLayouts())
	{
		return false;
	}

	// ---- 프레임/오브젝트 ResourceSet. 재질 셋은 재질마다 GetOrCreateGpuMaterial이 만듦 ----
	// 프레임 셋은 씬의 환경(스카이박스·IBL)이 바뀔 때 다시 만들어야 하므로 UpdateFrameSet 이 만듦. 처음에는
	// 빈 환경(검은 큐브·흰 LUT)을 꽂음.
	ResolveEnvironment(SceneEnvironment{});
	UpdateFrameSet();

	const ResourceSetDesc objectSet{
		.layout = m_layouts[Layout_Object],
		.bindings = { { .buffer = m_perObjectCB }, { .buffer = m_skinCB }, { .buffer = m_prevSkinCB }, { .buffer = m_oceanCB } },   // 레이아웃의 슬롯 순서: b1, b7, b13, b9
		.debugName = "Set_Object",
	};
	m_objectSet = m_device->CreateResourceSet(objectSet);

	const ResourceSetDesc blurWeightsSet{
		.layout = m_layouts[Layout_BlurWeights],
		.bindings = { { .buffer = m_postParamsCB }, { .buffer = m_blurWeights } },   // 레이아웃의 슬롯 순서: b4, u0
		.debugName = "Set_BlurWeights",
	};
	m_blurWeightsSet = m_device->CreateResourceSet(blurWeightsSet);

	m_uiCommonSet = m_device->CreateResourceSet(ResourceSetDesc{
		.layout = m_layouts[Layout_UICommon],
		.bindings = { { .buffer = m_uiCB }, { .sampler = GetSampler(SamplerPreset::LinearClamp) } },   // b4, s0
		.debugName = "Set_UICommon",
	});

	m_rainSet = m_device->CreateResourceSet(ResourceSetDesc{
		.layout = m_layouts[Layout_Rain],
		.bindings = { { .buffer = m_rainCB } },   // b8
		.debugName = "Set_Rain",
	});

	if (!m_frameSet.IsValid() || !m_objectSet.IsValid() || !m_blurWeightsSet.IsValid() || !m_uiCommonSet.IsValid() || !m_rainSet.IsValid())
	{
		Log::Error("Renderer::Initialize : ResourceSet 생성 실패.");
		return false;
	}

	// ---- 셰이더 (.cso 우선, 필요 시 컴파일) + 레이아웃 대조 ----
	if (!LoadShaders(false))
	{
		return false;
	}

	// ---- 패스별 PSO Desc 의 고정 부분 ----
	BuildPassDescs();

	// 첫 프레임 전에 기본 PSO를 만들어 두어 생성 실패를 초기화 단계에서 잡음.
	if (!GetPipelineFor(m_defaultMaterial, VertexFormat::PositionColorNormalTexcoordTangent).IsValid() ||
		!GetShadowPipelineFor(m_defaultMaterial, VertexFormat::PositionColorNormalTexcoordTangent).IsValid() ||
		!GetSkyPipeline().IsValid() ||
		!GetTonemapPipeline().IsValid() ||
		!GetPostPipeline(false).IsValid() ||
		!GetPostPipeline(true).IsValid() ||
		!GetGBufferPipelineFor(m_defaultMaterial, VertexFormat::PositionColorNormalTexcoordTangent, false).IsValid() ||
		!GetDeferredLightPipeline().IsValid() ||
		!GetSsaoPipeline(false).IsValid() ||
		!GetSsaoPipeline(true).IsValid())
	{
		Log::Error("Renderer::Initialize : 기본 PSO 생성 실패.");
		return false;
	}

	return true;
}

bool Renderer::CreateLayouts()
{
	// ---- 바인딩 레이아웃. "이 셋은 어떤 슬롯을 쓰는가"의 선언. 선언 사본을 m_layoutDescs 에 남겨 패스별 바인딩 대조가 씀 ----
	BindingLayoutDesc& frameLayout = m_layoutDescs[Layout_Frame];
	frameLayout = BindingLayoutDesc{};
	frameLayout.debugName = "Layout_Frame";
	frameLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kCBSlotPerFrame);
	frameLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotLights);
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotShadowMap);
	frameLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotShadow);
	// 환경 큐브맵. 재질 셋이 아니라 프레임 셋에 둠 — 씬마다 하나이고, 스카이박스와 (이후의) 반사·환경광이
	// 같은 큐브를 읽음.
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotEnvironment);
	frameLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotEnvironment);
	// IBL. 확산 조도 큐브, 반사 프리필터 큐브, BRDF LUT. 모두 환경 샘플러(s5)로 읽음.
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotIrradiance);
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotPrefiltered);
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotBrdfLut);
	// 다중 광원 그림자: b5 (그림자 패스가 뷰마다 올리는 행렬, VS), b6 (그림자 뷰 배열, PS). 프레임에 한 번
	// 정해지는 값이라 프레임 셋에 둠.
	frameLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex, kCBSlotShadowPass);
	frameLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotShadows);
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotCsmMap);   // t14: CSM 전용 아틀라스
	// 조명 배열. 텍스처가 아니라 구조체 버퍼 (StructuredBuffer<LightData>) — D3D12 에서는 루트 SRV 로 꽂힘.
	frameLayout.Add(BindingType::BufferResource, ShaderStageMask_Pixel, kSRVSlotLights);   // t15
	// 클러스터 (3b): 상자마다 (시작, 개수) t30, 조명 번호 목록 t31. 둘 다 구조체 버퍼 → D3D12 루트 SRV (2 DWORD 씩).
	frameLayout.Add(BindingType::BufferResource, ShaderStageMask_Pixel, kSRVSlotClusterGrid);     // t30
	frameLayout.Add(BindingType::BufferResource, ShaderStageMask_Pixel, kSRVSlotClusterLights);   // t31
	// 비 차폐 맵 t24 (docs/plan/rain-plan.html). 빗줄기 VS 가 빗방울을, 재질 PS 가 젖음을 판정하므로 VS·PS. 비가 없으면 흰 텍스처.
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kSRVSlotRainOcclusion);

	BindingLayoutDesc& objectLayout = m_layoutDescs[Layout_Object];
	objectLayout = BindingLayoutDesc{};
	objectLayout.debugName = "Layout_Object";
	objectLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kCBSlotPerObject);   // b1: 행렬은 VS, 강조 색(highlight*)은 PS
	objectLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex, kCBSlotSkin);                            // 스키닝 VS 만 읽음. 다른 VS 는 선언만 해 두고 안 씀
	objectLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex, kCBSlotPrevSkin);                        // b13 지난 프레임 팔레트 (메인·G-buffer 스킨 VS 의 속도)
	objectLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kCBSlotOcean);   // b9 바다 파도. 바다 VS·PS 만 읽음 (CS 충돌 셋의 b9 와는 한 패스에 같이 묶이지 않음)

	// 재질 셋: 상수(b3, uvScale 때문에 VS도), 알베도 t0, 노멀 t1, 금속성/거칠기 t2, AO t3, 발광 t4, 반사 세기 t5, 반사 색 t6, 클리어코트 t16~t18, 투과 t19, 디졸브 노이즈 t20, 샘플러 s0.
	BindingLayoutDesc& materialLayout = m_layoutDescs[Layout_Material];
	materialLayout = BindingLayoutDesc{};
	materialLayout.debugName = "Layout_Material";
	materialLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kCBSlotMaterial);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotAlbedo);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotNormal);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotMetallicRoughness);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotOcclusion);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotEmissive);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotSpecular);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotSpecularColor);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotClearcoat);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotClearcoatRoughness);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotClearcoatNormal);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotTransmission);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotDissolveNoise);   // t20 디졸브 노이즈
	materialLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotMaterial);

	// 후처리 셋: b0 (PerFrame: 노출·톤매핑·디버그 뷰), b4 (PostParams: 패스 파라미터), t13 (입력 텍스처), s6
	// (선형 Clamp). 슬롯 순서가 CreatePostSet 의 bindings 순서임. 모든 후처리 패스(톤매핑·블러·블룸)가 이
	// 레이아웃 하나를 씀 — 셰이더가 안 쓰는 슬롯도 항상 유효한 뷰가 꽂혀 있으므로 D3D12 디스크립터 테이블에도
	// 안전함.
	BindingLayoutDesc& postLayout = m_layoutDescs[Layout_Post];
	postLayout = BindingLayoutDesc{};
	postLayout.debugName = "Layout_Post";
	postLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotPerFrame);       // b0
	postLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotPostParams);     // b4
	postLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotSceneHdr);      // t13
	postLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotPost);             // s6
	postLayout.Add(BindingType::BufferResource, ShaderStageMask_Pixel, kSRVSlotBlurWeights);   // t20 (블러 가중치. 블러 패스만 읽음)

	// 컴퓨트 블러 셋: b4 (PostParams), t13 (입력), u0 (출력). 컴퓨트 슬롯은 그래픽스 스테이지와 섞지 않으므로
	// Layout_Post 를 같이 쓰지 않고 따로 둠. 입력 레지스터는 PS 블러와 같은 t13 (Post.hlsli 의 postInput. t0 은 재질 알베도).
	BindingLayoutDesc& blurLayout = m_layoutDescs[Layout_BlurCompute];
	blurLayout = BindingLayoutDesc{};
	blurLayout.debugName = "Layout_BlurCompute";
	blurLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Compute, kCBSlotPostParams);     // b4
	blurLayout.Add(BindingType::ShaderResource, ShaderStageMask_Compute, kSRVSlotSceneHdr);      // t13
	blurLayout.Add(BindingType::UnorderedAccess, ShaderStageMask_Compute, kUAVSlotPostOutput);   // u0
	blurLayout.Add(BindingType::BufferResource, ShaderStageMask_Compute, kSRVSlotBlurWeights);   // t20

	// 블러 가중치 셋: b4 (PostParams 의 시그마·반경), u0 (가중치 구조체 버퍼).
	BindingLayoutDesc& weightsLayout = m_layoutDescs[Layout_BlurWeights];
	weightsLayout = BindingLayoutDesc{};
	weightsLayout.debugName = "Layout_BlurWeights";
	weightsLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Compute, kCBSlotPostParams);            // b4
	weightsLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotBlurWeights);   // u0

	// 파티클 시뮬레이션 셋: b8 (이미터 상수), u0~u5 (파티클·사망 목록·생존 목록 둘·개수·간접 인자). 이미터마다
	// 두 벌 (생존 목록 입출력을 바꾼 것). 파티클 컴퓨트 패스 넷이 같이 씀.
	BindingLayoutDesc& particleSimLayout = m_layoutDescs[Layout_ParticleSim];
	particleSimLayout = BindingLayoutDesc{};
	particleSimLayout.debugName = "Layout_ParticleSim";
	particleSimLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Compute, kCBSlotParticle);                   // b8
	particleSimLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotParticles);          // u0
	particleSimLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotParticleDead);       // u1
	particleSimLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotParticleAliveIn);    // u2
	particleSimLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotParticleAliveOut);   // u3
	particleSimLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotParticleCounters);   // u4
	particleSimLayout.Add(BindingType::RawBufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotParticleArgs);    // u5

	// 파티클 시뮬레이션(적분) 셋: 위에서 u5 만 뺀 것. 적분은 간접 디스패치라 그동안 인자 버퍼가 IndirectArgument 상태이고,
	// 그 상태의 버퍼를 UAV 로 바인딩해 두면 안 됨 (셰이더가 u5 를 쓰지 않아도 바인딩 자체가 위험 — 두 백엔드가 거부함).
	BindingLayoutDesc& particleStepLayout = m_layoutDescs[Layout_ParticleStep];
	particleStepLayout = BindingLayoutDesc{};
	particleStepLayout.debugName = "Layout_ParticleStep";
	for (uint32_t i = 0; i + 1 < particleSimLayout.slotCount; ++i)
	{
		particleStepLayout.Add(particleSimLayout.slots[i].type, particleSimLayout.slots[i].stageMask, particleSimLayout.slots[i].reg);
	}

	// 파티클 그리기 셋 (셋 1, 프레임 셋 다음): b8 (VS 크기·색), t21 (파티클 버퍼), t22 (이번 프레임 생존 목록).
	BindingLayoutDesc& particleDrawLayout = m_layoutDescs[Layout_ParticleDraw];
	particleDrawLayout = BindingLayoutDesc{};
	particleDrawLayout.debugName = "Layout_ParticleDraw";
	particleDrawLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kCBSlotParticle);   // b8 (PS: 소프트·블렌드 모드)
	particleDrawLayout.Add(BindingType::BufferResource, ShaderStageMask_Vertex, kSRVSlotParticles);                         // t21
	particleDrawLayout.Add(BindingType::BufferResource, ShaderStageMask_Vertex, kSRVSlotParticleAlive);                     // t22

	// 파티클 깊이 셋 (셋 2, 뷰마다): t23 = 그 뷰의 깊이. 파티클 패스는 깊이를 읽기 전용 DSV 로 테스트하면서 같은
	// 깊이를 PS 가 읽음 (소프트 파티클).
	BindingLayoutDesc& particleDepthLayout = m_layoutDescs[Layout_ParticleDepth];
	particleDepthLayout = BindingLayoutDesc{};
	particleDepthLayout.debugName = "Layout_ParticleDepth";
	particleDepthLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotSceneDepth);   // t23

	// 파티클 충돌 셋 (적분 패스의 셋 1, 뷰마다 — 메인 뷰 것만 씀): b9 (메인 카메라), t23 (깊이).
	BindingLayoutDesc& particleCollideLayout = m_layoutDescs[Layout_ParticleCollide];
	particleCollideLayout = BindingLayoutDesc{};
	particleCollideLayout.debugName = "Layout_ParticleCollide";
	particleCollideLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Compute, kCBSlotParticleCollision);   // b9
	particleCollideLayout.Add(BindingType::ShaderResource, ShaderStageMask_Compute, kSRVSlotSceneDepth);         // t23

	// 알파 이미터 정렬 셋 (셋 0, 이미터·뷰·parity 마다): b8 (capacity), b10 (뷰 행렬), t21 파티클, t22 생존 목록, t24 개수,
	// u0 키, u1 번호. 단계 셋 (셋 1, 단계마다): b11 (k, j).
	BindingLayoutDesc& particleSortLayout = m_layoutDescs[Layout_ParticleSort];
	particleSortLayout = BindingLayoutDesc{};
	particleSortLayout.debugName = "Layout_ParticleSort";
	particleSortLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Compute, kCBSlotParticle);              // b8
	particleSortLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Compute, kCBSlotParticleSortView);      // b10
	particleSortLayout.Add(BindingType::BufferResource, ShaderStageMask_Compute, kSRVSlotParticles);            // t21
	particleSortLayout.Add(BindingType::BufferResource, ShaderStageMask_Compute, kSRVSlotParticleAlive);        // t22
	particleSortLayout.Add(BindingType::BufferResource, ShaderStageMask_Compute, kSRVSlotParticleCounters);     // t24
	particleSortLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotSortKeys);      // u0
	particleSortLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotSortIndices);   // u1
	BindingLayoutDesc& particleSortStepLayout = m_layoutDescs[Layout_ParticleSortStep];
	particleSortStepLayout = BindingLayoutDesc{};
	particleSortStepLayout.debugName = "Layout_ParticleSortStep";
	particleSortStepLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Compute, kCBSlotParticleSortStep);   // b11

	// G-buffer 셋 (디퍼드 라이팅의 셋 1, 뷰마다): t23 깊이 (DepthRead), t25~t28 G-buffer, t29 SSAO 블러 결과. 전부 Load 로
	// 읽어 샘플러가 없음.
	BindingLayoutDesc& gbufferLayout = m_layoutDescs[Layout_GBuffer];
	gbufferLayout = BindingLayoutDesc{};
	gbufferLayout.debugName = "Layout_GBuffer";
	gbufferLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotSceneDepth);   // t23
	gbufferLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotGBuffer0);     // t25
	gbufferLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotGBuffer1);     // t26
	gbufferLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotGBuffer2);     // t27
	gbufferLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotGBuffer3);     // t28
	gbufferLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotSsao);         // t29

	// SSAO 셋 (SSAO·블러 패스의 셋 0, 뷰마다 두 벌): b0 (proj·invViewProj·화면 크기), b12 (반경·커널), t23 깊이, t26 노멀,
	// t29 입력 AO (블러 셋만 의미 있음 — SSAO 셋은 흰색을 꽂음).
	BindingLayoutDesc& ssaoLayout = m_layoutDescs[Layout_Ssao];
	ssaoLayout = BindingLayoutDesc{};
	ssaoLayout.debugName = "Layout_Ssao";
	ssaoLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotPerFrame);      // b0
	ssaoLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotSsao);          // b12
	ssaoLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotSceneDepth);   // t23
	ssaoLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotGBuffer1);     // t26
	ssaoLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotSsao);         // t29

	// 시간 셋 (속도 디버그·모션 블러·TAA 의 셋 0, 뷰마다): b0 (지터 없는 행렬·화면 크기), b4 (패스 파라미터), t13 입력 (hdr),
	// t23 깊이, t7 속도, t24 히스토리 (TAA 만 의미 있음 — 다른 셋은 흰 텍스처), s6 선형 Clamp.
	BindingLayoutDesc& temporalLayout = m_layoutDescs[Layout_Temporal];
	temporalLayout = BindingLayoutDesc{};
	temporalLayout.debugName = "Layout_Temporal";
	temporalLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotPerFrame);      // b0
	temporalLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotPostParams);    // b4
	temporalLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotSceneHdr);     // t13
	temporalLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotSceneDepth);   // t23
	temporalLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotVelocity);     // t7
	temporalLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotHistory);      // t24
	temporalLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotPost);            // s6

	// GPU 클러스터 비닝 셋 (컴퓨트 다섯 패스의 셋 0, 뷰마다 한 벌): b13 뷰 값, t7 월드 경계 구, u0~u5 (Shaders/ClusterBin.hlsli).
	// u3·u4 는 프레임 셋 t30·t31 과 같은 버퍼 (GPU 경로) — 디스패치 전에 UnorderedAccess, 그리기 전에 ShaderResource 로 바꿈.
	BindingLayoutDesc& clusterBinLayout = m_layoutDescs[Layout_ClusterBin];
	clusterBinLayout = BindingLayoutDesc{};
	clusterBinLayout.debugName = "Layout_ClusterBin";
	clusterBinLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Compute, kCBSlotClusterBin);                  // b13
	clusterBinLayout.Add(BindingType::BufferResource, ShaderStageMask_Compute, kSRVSlotLightBounds);                // t7
	clusterBinLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotClusterViewBounds);   // u0
	clusterBinLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotClusterTileCounts);   // u1
	clusterBinLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotClusterTileMasks);    // u2
	clusterBinLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotClusterGrid);         // u3
	clusterBinLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotClusterLights);       // u4
	clusterBinLayout.Add(BindingType::BufferUnorderedAccess, ShaderStageMask_Compute, kUAVSlotClusterStats);        // u5

	// 게임 UI: 셋 0 = { b4 UI 상수, s0 } 하나, 셋 1 = { t0 } 텍스처마다. 샘플러를 텍스처 셋에서 뺀 이유는 ShaderConstants.h 의 kCBSlotUI 주석
	BindingLayoutDesc& uiCommonLayout = m_layoutDescs[Layout_UICommon];
	uiCommonLayout = BindingLayoutDesc{};
	uiCommonLayout.debugName = "Layout_UICommon";
	uiCommonLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex, kCBSlotUI);   // b4
	uiCommonLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotUI);      // s0
	BindingLayoutDesc& uiTextureLayout = m_layoutDescs[Layout_UITexture];
	uiTextureLayout = BindingLayoutDesc{};
	uiTextureLayout.debugName = "Layout_UITexture";
	uiTextureLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotUITexture);   // t0

	// 비 셋 (빗줄기·튐 패스의 셋 1): b8 RainConstants. 프레임 셋(b0, t24)·파티클 깊이 셋(t23)과 같이 묶임.
	BindingLayoutDesc& rainLayout = m_layoutDescs[Layout_Rain];
	rainLayout = BindingLayoutDesc{};
	rainLayout.debugName = "Layout_Rain";
	rainLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kCBSlotRain);   // b8

	for (uint32_t i = 0; i < Layout_Count; ++i)
	{
		m_layouts[i] = m_device->CreateBindingLayout(m_layoutDescs[i]);
		if (!m_layouts[i].IsValid())
		{
			Log::Error("Renderer::Initialize : BindingLayout 생성 실패 (%s).", m_layoutDescs[i].debugName);
			return false;
		}
	}
	return true;
}

void Renderer::BuildPassDescs()
{
	// 셰이더와 레이아웃은 표에서. 지금의 셰이더 핸들을 쓰므로 핫리로드 뒤에 다시 부르면 새 핸들이 들어감.
	for (uint32_t p = 0; p < Pass_Count; ++p)
	{
		const PassInfo& info = kPasses[p];
		PipelineStateDesc desc;
		desc.vs = m_shaders[info.vs];
		desc.ps = info.ps < Shader_Count ? m_shaders[info.ps] : ShaderHandle{};
		for (uint32_t i = 0; i < info.layoutCount; ++i)
		{
			desc.bindingLayouts[i] = m_layouts[info.layouts[i]];
		}
		desc.bindingLayoutCount = static_cast<uint8_t>(info.layoutCount);
		m_passDesc[p] = desc;
	}

	// 나머지(깊이 테스트 켬, 불투명, 트라이앵글 리스트)는 Desc 기본값. 패스마다 다른 부분만 적음.
	// 메인 패스: 재질·설정에 따라 달라지는 필드(fill, cull, 정점 레이아웃, 타깃 포맷)는 GetPipelineFor 가 채움.

	// 그림자 패스: VS 만(ps 는 빈 핸들. 알파 테스트 변형 Pass_ShadowAlpha* 만 PS 가 있음), 렌더 타깃 없음, 깊이 D32_FLOAT. 래스터라이저 깊이 바이어스로 자기
	// 그림자(acne)를 줄임. D32_FLOAT 에서 DepthBias 단위는 2^(지수−23) — z ≈ 0.5 면 한 단위가 6e-8 임.
	for (PassId id : { Pass_Shadow, Pass_ShadowSkin, Pass_ShadowAlpha, Pass_ShadowAlphaSkin })
	{
		PipelineStateDesc& shadow = m_passDesc[id];
		shadow.rasterizer.depthBias = 2000;
		shadow.rasterizer.depthBiasClamp = 0.01f;
		shadow.rasterizer.slopeScaledDepthBias = 2.0f;
		shadow.rtvCount = 0;
		shadow.dsvFormat = Format::D32_FLOAT;
	}

	// 디버그 선: 선 목록, 깊이 테스트 없음 (메시 안의 본도 보이게), 컬링 없음. 정점 = 월드 위치(float3) + 색(R8G8B8A8_UNORM).
	{
		PipelineStateDesc& line = m_passDesc[Pass_DebugLine];
		line.topology = PrimitiveTopology::LineList;
		line.rasterizer.cull = CullMode::None;
		line.depthStencil.depthEnable = false;
		line.depthStencil.depthWrite = false;
		line.vertexLayout.attributes[0] = { .semantic = VertexSemantic::Position, .format = Format::R32G32B32_FLOAT, .offset = static_cast<uint16_t>(offsetof(DebugLineVertex, position)) };
		line.vertexLayout.attributes[1] = { .semantic = VertexSemantic::Color, .format = Format::R8G8B8A8_UNORM, .offset = static_cast<uint16_t>(offsetof(DebugLineVertex, color)) };
		line.vertexLayout.attributeCount = 2;
		line.vertexLayout.stride = sizeof(DebugLineVertex);
	}

	// 스카이박스. 정점 입력이 없으므로 vertexLayout 은 기본값(속성 0개) 그대로 둠 — 입력 레이아웃 없이
	// SV_VertexID 만 씀. 깊이: 테스트는 켜고(LessEqual — VS 가 만든 깊이 1.0 이 클리어 값 1.0 과 같아도 통과)
	// 쓰기는 끔. 컬링: 전체 화면 삼각형 하나라 끔.
	PipelineStateDesc& sky = m_passDesc[Pass_Sky];
	sky.rasterizer.cull = CullMode::None;
	sky.depthStencil.depthEnable = true;
	sky.depthStencil.depthWrite = false;
	sky.depthStencil.depthFunc = CompareFunc::LessEqual;

	// 파티클: 정점 입력 없음 (SV_VertexID), 컬링 없음 (빌보드), 깊이 테스트만 (불투명 뒤에 가려지되 서로는 가리지 않음),
	// 프리멀티플라이드 블렌딩 하나로 가산·알파 두 모드를 그림 (알파 이미터는 SortParticles 가 뷰마다 먼 것부터 정렬). 목적지 알파는 유지.
	{
		PipelineStateDesc& particle = m_passDesc[Pass_Particle];
		particle.rasterizer.cull = CullMode::None;
		particle.depthStencil.depthEnable = true;
		particle.depthStencil.depthWrite = false;
		particle.blend.rt[0] = RenderTargetBlendDesc{
			.blendEnable = true,
			// 프리멀티플라이드: 가산 이미터는 알파 0 을 내보내 목적지 × 1 + 색 (예전의 One + One 과 같은 결과),
			// 알파 이미터는 색 × 알파와 알파를 내보내 목적지 × (1 − 알파) + 색. PSO 하나로 두 모드를 그림.
			.srcColor = BlendFactor::One,
			.dstColor = BlendFactor::InvSrcAlpha,
			.colorOp = BlendOp::Add,
			.srcAlpha = BlendFactor::Zero,
			.dstAlpha = BlendFactor::One,
			.alphaOp = BlendOp::Add,
		};
		particle.rtvFormats[0] = Format::R16G16B16A16_FLOAT;   // 메인 패스와 같은 타깃 (FP16 HDR + D24S8)
		particle.rtvCount = 1;
		particle.dsvFormat = Format::D24_UNORM_S8_UINT;
		// 비 (빗줄기·튐): 파티클과 같은 상태 — 정점 입력 없음, 컬링 없음, 깊이 테스트만, 알파 0 을 내보내는 가산. 셰이더·셋만 다름.
		for (PassId id : { Pass_Rain, Pass_RainSplash })
		{
			PipelineStateDesc& rain = m_passDesc[id];
			const PipelineStateDesc fixed = rain;   // 표에서 온 셰이더·레이아웃
			rain = particle;
			rain.vs = fixed.vs;
			rain.ps = fixed.ps;
			for (uint32_t i = 0; i < kMaxBindingSets; ++i)
			{
				rain.bindingLayouts[i] = fixed.bindingLayouts[i];
			}
			rain.bindingLayoutCount = fixed.bindingLayoutCount;
		}
	}

	// 톤 패스와 블러·블룸: 풀스크린 삼각형, 깊이 없음 (dsvFormat 이 Unknown 이어야 깊이 없는 패스에 쓸 수 있음).
	for (PassId id : { Pass_Tonemap, Pass_Post, Pass_VelocityDebug, Pass_MotionBlur, Pass_Taa })
	{
		PipelineStateDesc& desc = m_passDesc[id];
		desc.rasterizer.cull = CullMode::None;
		desc.depthStencil.depthEnable = false;
		desc.depthStencil.depthWrite = false;
		desc.dsvFormat = Format::Unknown;
	}

	// 디퍼드 라이팅·SSAO: 풀스크린 삼각형, 깊이 없음. 라이팅은 view.hdr 에 가산 (G-buffer 패스가 쓴 발광 + 조명. 알파 유지),
	// SSAO 는 R8 타깃에 덮어씀. 타깃 포맷은 Get*Pipeline 이 채움.
	for (PassId id : { Pass_DeferredLight, Pass_Ssao, Pass_SsaoBlur })
	{
		PipelineStateDesc& desc = m_passDesc[id];
		desc.rasterizer.cull = CullMode::None;
		desc.depthStencil.depthEnable = false;
		desc.depthStencil.depthWrite = false;
		desc.dsvFormat = Format::Unknown;
	}
	m_passDesc[Pass_DeferredLight].blend.rt[0] = RenderTargetBlendDesc{
		.blendEnable = true,
		.srcColor = BlendFactor::One,
		.dstColor = BlendFactor::One,
		.colorOp = BlendOp::Add,
		.srcAlpha = BlendFactor::Zero,
		.dstAlpha = BlendFactor::One,
		.alphaOp = BlendOp::Add,
	};

	// 가산 버전은 블룸 합성용: 출력 = 셰이더 출력 + 기존 값 (알파는 기존 값 유지).
	m_postAddDesc = m_passDesc[Pass_Post];
	m_postAddDesc.blend.rt[0] = RenderTargetBlendDesc{
		.blendEnable = true,
		.srcColor = BlendFactor::One,
		.dstColor = BlendFactor::One,
		.colorOp = BlendOp::Add,
		.srcAlpha = BlendFactor::Zero,
		.dstAlpha = BlendFactor::One,
		.alphaOp = BlendOp::Add,
	};

	// 게임 UI: 정점 = 캔버스 위치 float2 + uv float2 + 색 RGBA8 + 클립 float4 + 캔버스 번호 uint (UI/UIDrawList.h 의 UIVertex).
	// 깊이 없음, 컬링 없음, 알파 블렌딩 (색: SrcAlpha/InvSrcAlpha, 알파: One/InvSrcAlpha). 타깃 = 출력 텍스처의 UNORM 뷰.
	{
		PipelineStateDesc& ui = m_passDesc[Pass_UI];
		ui.rasterizer.cull = CullMode::None;
		ui.depthStencil.depthEnable = false;
		ui.depthStencil.depthWrite = false;
		ui.dsvFormat = Format::Unknown;
		ui.rtvFormats[0] = Format::R8G8B8A8_UNORM;
		ui.rtvCount = 1;
		ui.blend.rt[0] = RenderTargetBlendDesc{
			.blendEnable = true,
			.srcColor = BlendFactor::SrcAlpha,
			.dstColor = BlendFactor::InvSrcAlpha,
			.colorOp = BlendOp::Add,
			.srcAlpha = BlendFactor::One,
			.dstAlpha = BlendFactor::InvSrcAlpha,
			.alphaOp = BlendOp::Add,
		};
		ui.vertexLayout.attributes[0] = { .semantic = VertexSemantic::Position, .format = Format::R32G32_FLOAT, .offset = static_cast<uint16_t>(offsetof(UIVertex, position)) };
		ui.vertexLayout.attributes[1] = { .semantic = VertexSemantic::TexCoord, .format = Format::R32G32_FLOAT, .offset = static_cast<uint16_t>(offsetof(UIVertex, uv)) };
		ui.vertexLayout.attributes[2] = { .semantic = VertexSemantic::Color, .format = Format::R8G8B8A8_UNORM, .offset = static_cast<uint16_t>(offsetof(UIVertex, color)) };
		ui.vertexLayout.attributes[3] = { .semantic = VertexSemantic::TexCoord, .semanticIndex = 1, .format = Format::R32G32B32A32_FLOAT, .offset = static_cast<uint16_t>(offsetof(UIVertex, clip)) };
		ui.vertexLayout.attributes[4] = { .semantic = VertexSemantic::BlendIndices, .format = Format::R32_UINT, .offset = static_cast<uint16_t>(offsetof(UIVertex, canvas)) };
		ui.vertexLayout.attributeCount = 5;
		ui.vertexLayout.stride = sizeof(UIVertex);

		// World 캔버스: 장면 깊이로 가려짐. 깊이는 TAA 지터가 들어간 것이고 UI 는 지터 없이 그려 경계가 1 px 안쪽에서 흔들릴 수 있음 (3.2 한계)
		PipelineStateDesc& world = m_passDesc[Pass_UIWorld];
		world = ui;   // 같은 셰이더·레이아웃 (kPasses 의 두 줄이 같음)
		world.depthStencil.depthEnable = true;
		world.depthStencil.depthWrite = false;
		world.depthStencil.depthFunc = CompareFunc::LessEqual;
		world.dsvFormat = Format::D24_UNORM_S8_UINT;
	}

	// Desc 가 바뀌었으므로 (새 셰이더 핸들) 작은 키 → PSO 핸들 캐시는 전부 무효.
	m_uiPipeline = PipelineHandle{};
	m_uiWorldPipeline = PipelineHandle{};
	m_mainPipelines.clear();
	m_shadowPipelines.clear();
	m_gbufferPipelines.clear();
	m_deferredLightPipeline = PipelineHandle{};
	for (PipelineHandle& handle : m_ssaoPipelines)
	{
		handle = PipelineHandle{};
	}
	m_skyPipeline = PipelineHandle{};
	m_debugLinePipeline = PipelineHandle{};
	m_particlePipeline = PipelineHandle{};
	m_rainPipeline = PipelineHandle{};
	m_rainSplashPipeline = PipelineHandle{};
	for (PipelineHandle& handle : m_tonemapPipelines)
	{
		handle = PipelineHandle{};
	}
	for (PipelineHandle& handle : m_postPipelines)
	{
		handle = PipelineHandle{};
	}
	for (PipelineHandle& handle : m_temporalPipelines)
	{
		handle = PipelineHandle{};
	}
	for (ComputePipelineHandle& handle : m_computePipelines)
	{
		handle = ComputePipelineHandle{};
	}
	for (bool& tried : m_computeTried)
	{
		tried = false;
	}
}

bool Renderer::CreateSamplers()
{
	// SamplerPreset 순서와 같아야 함.
	struct PresetDesc
	{
		SamplerFilter filter;
		SamplerAddress address;
		float maxLod;
		const char* name;
	};
	const PresetDesc presets[kSamplerPresetCount] = {
		{ SamplerFilter::Linear, SamplerAddress::Wrap, 1000.0f, "Sampler_LinearWrap" },
		{ SamplerFilter::Linear, SamplerAddress::Clamp, 1000.0f, "Sampler_LinearClamp" },
		{ SamplerFilter::Anisotropic, SamplerAddress::Wrap, 1000.0f, "Sampler_AnisotropicWrap" },
		{ SamplerFilter::Point, SamplerAddress::Wrap, 1000.0f, "Sampler_PointWrap" },
		{ SamplerFilter::Point, SamplerAddress::Wrap, 0.0f, "Sampler_PointNoMip" },   // maxLod 0 = 밉 0만
	};
	for (uint32_t i = 0; i < kSamplerPresetCount; ++i)
	{
		const SamplerDesc desc{
			.filter = presets[i].filter,
			.addressU = presets[i].address,
			.addressV = presets[i].address,
			.addressW = presets[i].address,
			.maxAnisotropy = 16,
			.maxLod = presets[i].maxLod,
			.debugName = presets[i].name,
		};
		m_samplers[i] = m_device->CreateSampler(desc);
		if (!m_samplers[i].IsValid())
		{
			Log::Error("Renderer : 샘플러 생성 실패 (%s).", presets[i].name);
			return false;
		}
	}
	return true;
}

void Renderer::ResolveEnvironment(const SceneEnvironment& environment)
{
	EnvironmentBinding binding;
	binding.skybox = binding.irradiance = binding.specular = m_textures.GetBlackCube();
	binding.brdfLut = m_textures.GetWhite();

	// 실패한 이름은 TextureCache::LoadCube 가 검은 큐브를 돌려주므로 "검은 큐브가 아님" = 로드 성공.
	auto load = [this](const std::string& name, uint32_t& mips) -> TextureHandle
	{
		if (name.empty()) return TextureHandle{};
		const TextureHandle handle = m_textures.LoadCube(name, &mips);
		return handle != m_textures.GetBlackCube() ? handle : TextureHandle{};
	};
	uint32_t skyMips = 1, irradianceMips = 1, specularMips = 1;
	const TextureHandle sky = load(environment.skybox, skyMips);
	const TextureHandle irradiance = load(environment.irradiance, irradianceMips);
	const TextureHandle specular = load(environment.specular, specularMips);
	if (sky.IsValid())
	{
		binding.skybox = sky;
	}

	// 반사: 프리필터 큐브 → 없으면 스카이박스의 밉 체인으로 근사(박스 필터라 GGX 로 거른 것보다 거칠기 모양이 덜 맞음).
	if (specular.IsValid())
	{
		binding.specular = specular;
		binding.specularMips = specularMips;
	}
	else if (sky.IsValid())
	{
		binding.specular = sky;
		binding.specularMips = skyMips;
	}
	// 확산: 조도 큐브 → 없으면 스카이박스(또는 반사 큐브)의 끝에서 두 번째 밉. 면당 2×2 라 위·아래·옆의 밝기
	// 차이 정도만 남음.
	if (irradiance.IsValid())
	{
		binding.irradiance = irradiance;
		binding.irradianceLod = 0.0f;
	}
	else if (binding.specular != m_textures.GetBlackCube())
	{
		binding.irradiance = binding.specular;
		binding.irradianceLod = static_cast<float>(binding.specularMips >= 2 ? binding.specularMips - 2 : 0);
	}
	binding.hasEnvironment = irradiance.IsValid() || binding.specular != m_textures.GetBlackCube();

	// BRDF LUT 는 2D 텍스처 캐시를 씀 (선형, 부동소수점 DDS 면 HDR 경로). 실패하면 흰색이 돌아오므로 그것으로 판단함.
	if (!environment.brdfLut.empty())
	{
		const TextureHandle lut = m_textures.Load2D(environment.brdfLut, false, nullptr);
		if (lut != m_textures.GetWhite())
		{
			binding.brdfLut = lut;
			binding.hasBrdfLut = true;
		}
	}

	binding.iblIntensity = environment.iblIntensity;
	binding.iblTint = environment.iblTint;
	binding.exposure = environment.exposure;
	binding.toneMapping = environment.toneMapping;
	m_environment = binding;
	m_postSettings = environment.post;
	if (m_settings.motionBlurOverride >= 0)
	{
		m_postSettings.motionBlurEnabled = m_settings.motionBlurOverride != 0;   // --motion-blur= 실행 인자
	}
	if (m_settings.motionBlurFpsOverride >= 0.0f)
	{
		m_postSettings.motionBlurTargetFps = m_settings.motionBlurFpsOverride;   // --motion-blur-fps=
	}
	if (m_settings.taaOverride >= 0)
	{
		m_postSettings.taaEnabled = m_settings.taaOverride != 0;   // --taa= 실행 인자
	}
}

bool Renderer::UpdateFrameSet()
{
	// t24 비 차폐 맵: 비가 있으면 맵, 없으면 흰 텍스처 (D3D12 테이블에는 유효한 뷰가 있어야 함. 셰이더는 rainParams.z 로 쓸지 정함).
	const TextureHandle rainMap = (m_frameRain.active && m_rainMap.IsValid()) ? m_rainMap : m_textures.GetWhite();
	if (m_frameSet.IsValid() && m_environment.SameTextures(m_boundEnvironment) && m_frameSetClusterGpu == m_clusterGpuThisFrame && m_frameSetRainMap == rainMap) return true;

	// ResourceSet 은 값 객체라 슬롯 하나만 바꾸는 기능이 없음 — 재질 셋과 같이 통째로 다시 만듦 (D3D12
	// 디스크립터 테이블도 다시 씀). 이전 셋은 D3D12 에서 지연 해제되므로, 이미 기록된 드로우가 그 테이블을
	// 읽어도 안전함.
	m_device->DestroyResourceSet(m_frameSet);
	// bindings[i] 는 CreateLayouts 의 frameLayout.Add(...) i 번째 슬롯과 짝임: b0, b2, t8, s4, t9, s5, t10, t11,
	// t12, b5, b6, t14, t15, t30, t31, t24. 환경 샘플러는 재질 프리셋의 LinearClamp 를 함께 씀 (큐브 샘플은 면 경계를 하드웨어가
	// 이어 주고, LUT 는 가장자리에서 반복되면 안 됨).
	const ResourceSetDesc frameSet{
		.layout = m_layouts[Layout_Frame],
		.bindings = {
			{ .buffer = m_perFrameCB },
			{ .buffer = m_lightCB },
			{ .texture = m_shadowMap },
			{ .sampler = m_shadowSampler },
			{ .texture = m_environment.skybox },
			{ .sampler = GetSampler(SamplerPreset::LinearClamp) },
			{ .texture = m_environment.irradiance },
			{ .texture = m_environment.specular },
			{ .texture = m_environment.brdfLut },
			{ .buffer = m_shadowPassCB },
			{ .buffer = m_shadowsCB },
			{ .texture = m_csmMap },
			{ .buffer = m_lightBuffer },
			// t30·t31: CPU 비닝이면 Dynamic 버퍼 (뷰마다 올림), GPU 비닝이면 컴퓨트가 쓰는 Default 버퍼
			{ .buffer = m_clusterGpuThisFrame ? m_clusterGridGpu : m_clusterGridBuffer },
			{ .buffer = m_clusterGpuThisFrame ? m_clusterIndexGpu : m_clusterIndexBuffer },
			{ .texture = rainMap },   // t24 비 차폐 맵
		},
		.debugName = "Set_Frame",
	};
	m_frameSet = m_device->CreateResourceSet(frameSet);
	m_boundEnvironment = m_environment;
	m_frameSetClusterGpu = m_clusterGpuThisFrame;
	m_frameSetRainMap = rainMap;
	if (!m_frameSet.IsValid())
	{
		Log::Error("Renderer : 프레임 ResourceSet 생성 실패.");
		return false;
	}
	return true;
}

SamplerHandle Renderer::GetSampler(SamplerPreset preset) const
{
	const uint32_t index = static_cast<uint32_t>(preset);
	return index < kSamplerPresetCount ? m_samplers[index] : m_samplers[0];
}

bool Renderer::LoadShaders(bool fromSourceOnly)
{
	std::vector<uint8_t> code[Shader_Count];
	std::wstring source[Shader_Count];
	for (uint32_t i = 0; i < Shader_Count; ++i)
	{
		const ShaderInfo& info = kShaders[i];
		if (fromSourceOnly)
		{
			source[i] = Paths::GetShaderSourcePath(info.file);
			if (!ShaderCompiler::CompileFromFile(source[i], info.entry, info.stage, code[i])) return false;
		}
		else if (!ShaderCompiler::LoadOrCompile(info.file, info.entry, info.stage, code[i], &source[i]))
		{
			return false;
		}
	}

#if defined(_DEBUG)
	// HLSL cbuffer 크기와 C++ 구조체 크기를 리플렉션으로 대조함. 셰이더가 선언한 cbuffer 를 전부 읽어 표
	// (kConstantBuffers)와 맞춰 봄 — 셰이더마다 "무엇을 검사할지"를 손으로 적지 않음. 어긋나면 실패.
	bool layoutOk = true;
	for (uint32_t i = 0; i < Shader_Count; ++i)
	{
		std::vector<ShaderCompiler::ReflectedConstantBuffer> buffers;
		if (!ShaderCompiler::ReflectConstantBuffers(code[i], buffers)) continue;   // 리플렉션이 불가능하면 검사 생략
		for (const auto& buffer : buffers)
		{
			const ConstantBufferInfo* expected = nullptr;
			for (const ConstantBufferInfo& info : kConstantBuffers)
			{
				if (buffer.name == info.name)
				{
					expected = &info;
					break;
				}
			}
			if (expected == nullptr)
			{
				Log::Error("%s : cbuffer '%s' 가 kConstantBuffers 표에 없음. C++ 구조체와 함께 표에 추가할 것.", kShaders[i].name, buffer.name.c_str());
				layoutOk = false;
			}
			else if (buffer.size != expected->size)
			{
				Log::Error("%s : cbuffer '%s' 크기 불일치: HLSL %u 바이트, C++ %u 바이트. 패킹 규칙(float3 뒤 스칼라)을 확인할 것.", kShaders[i].name, buffer.name.c_str(), buffer.size, expected->size);
				layoutOk = false;
			}
			else
			{
				Log::Info("cbuffer '%s' 크기 일치: %u 바이트 (변수 %u개, %s)", buffer.name.c_str(), buffer.size, buffer.variables, kShaders[i].name);
			}
		}
	}
	if (!layoutOk)
	{
		Log::Error("Renderer : 상수버퍼 레이아웃 불일치.");
		return false;
	}
#endif

	// 셰이더가 실제로 쓰는 슬롯이 그 패스의 레이아웃 안에 있는지 검사함. Release에서는 경고만 함.
	if (!ValidateBindings(code))
	{
#if defined(_DEBUG)
		return false;
#endif
	}

	ShaderHandle created[Shader_Count];
	bool allValid = true;
	for (uint32_t i = 0; i < Shader_Count; ++i)
	{
		// ShaderDesc::bytecode 는 std::span — vector 의 data()/size() 를 따로 옮기지 않음.
		const ShaderDesc desc{ .stage = kShaders[i].stage, .bytecode = code[i], .debugName = kShaders[i].name };
		created[i] = m_device->CreateShader(desc);
		allValid = allValid && created[i].IsValid();
	}
	if (!allValid)
	{
		Log::Error("Renderer : 셰이더 생성 실패.");
		for (ShaderHandle handle : created)
		{
			m_device->DestroyShader(handle);
		}
		return false;
	}

	// 이전 셰이더는 PSO가 참조로 붙들고 있으므로 풀에서 지워도 캐시가 비워질 때까지 살아 있음.
	for (uint32_t i = 0; i < Shader_Count; ++i)
	{
		m_device->DestroyShader(m_shaders[i]);
		m_shaders[i] = created[i];
	}

	// 핫리로드 감시 목록: 표의 소스 파일 전부 + 그 파일들이 include 하는 헤더 전부. 목록은 로드할 때마다 다시
	// 만들므로 새로 include 한 헤더는 다음 리로드부터 감시됨. Release에서도 목록은 만들지만 CheckHotReload가 동작하지 않음.
	m_watched.clear();
	auto watch = [this](const std::wstring& path)
	{
		for (const WatchedFile& existing : m_watched)
		{
			if (existing.path == path) return;   // 같은 파일을 두 셰이더가 쓰는 경우
		}
		WatchedFile watched;
		watched.path = path;
		watched.writeTime = ShaderCompiler::GetLastWriteTime(path);
		m_watched.push_back(watched);
	};
	for (const ShaderInfo& info : kShaders)
	{
		const std::wstring source = Paths::GetShaderSourcePath(info.file);
		watch(source);
		for (const std::wstring& include : ShaderCompiler::ListIncludeFiles(source))
		{
			watch(include);
		}
	}

	std::string loaded;
	for (uint32_t i = 0; i < Shader_Count; ++i)
	{
		if (!loaded.empty())
		{
			loaded += ", ";
		}
		loaded += kShaders[i].name;
		loaded += " ";
		loaded += Log::ToUtf8(source[i].c_str());
	}
	Log::Info("셰이더 로드 (%u개): %s", static_cast<uint32_t>(Shader_Count), loaded.c_str());
	return true;
}

bool Renderer::ValidateBindings(const std::vector<uint8_t>* code) const
{
	bool ok = true;
	for (uint32_t p = 0; p < Pass_Count; ++p)
	{
		const PassInfo& pass = kPasses[p];

		// (1) 이 패스의 레이아웃끼리 같은 레지스터를 같은 스테이지에 선언하면 안 됨. 레지스터는 PSO 하나
		//     안에서만 유일하면 되므로 (다른 패스는 같은 번호를 다른 용도로 써도 됨) 검사 단위가 패스임.
		for (uint32_t a = 0; a < pass.layoutCount; ++a)
		{
			for (uint32_t b = a + 1; b < pass.layoutCount; ++b)
			{
				const BindingLayoutDesc& la = m_layoutDescs[pass.layouts[a]];
				const BindingLayoutDesc& lb = m_layoutDescs[pass.layouts[b]];
				for (uint32_t i = 0; i < la.slotCount; ++i)
				{
					for (uint32_t j = 0; j < lb.slotCount; ++j)
					{
						const BindingSlot& x = la.slots[i];
						const BindingSlot& y = lb.slots[j];
						if (RegisterClassOf(x.type) == RegisterClassOf(y.type) && x.reg == y.reg && (x.stageMask & y.stageMask) != 0)
						{
							Log::Error("바인딩 대조: [%s] %s 와 %s 가 같은 레지스터 %c%u 를 같은 스테이지에 선언함.", pass.name, la.debugName, lb.debugName, RegisterClassOf(x.type), x.reg);
							ok = false;
						}
					}
				}
			}
		}

		// (2) 셰이더가 쓰는 슬롯마다: 이 패스의 레이아웃 중 하나에 같은 종류·번호가 그 스테이지로 선언돼 있어야 함.
		auto check = [&](uint32_t shader, uint8_t stage)
		{
			if (shader >= Shader_Count) return;
			std::vector<ShaderCompiler::ReflectedBinding> used;
			if (!ShaderCompiler::ReflectBindings(code[shader], used)) return;   // 리플렉션이 불가능하면 검사 생략
			for (const auto& u : used)
			{
				bool found = false;
				for (uint32_t l = 0; l < pass.layoutCount && !found; ++l)
				{
					const BindingLayoutDesc& layout = m_layoutDescs[pass.layouts[l]];
					for (uint32_t i = 0; i < layout.slotCount; ++i)
					{
						const BindingSlot& slot = layout.slots[i];
						if (slot.type == u.type && slot.reg == u.reg && (slot.stageMask & stage))
						{
							found = true;
							break;
						}
					}
				}
				const char typeName = RegisterClassOf(u.type);
				if (!found)
				{
					Log::Error("바인딩 대조: [%s] %s가 %c%u ('%s')를 쓰지만 이 패스의 BindingLayout에 그 스테이지로 선언되지 않음.", pass.name, kShaders[shader].name, typeName, u.reg, u.name.c_str());
					ok = false;
				}
				else
				{
					Log::Info("바인딩 대조: [%s] %s %c%u ('%s') OK", pass.name, kShaders[shader].name, typeName, u.reg, u.name.c_str());
				}
			}
		};
		check(pass.vs, ShaderStageMask_Vertex);
		check(pass.ps, ShaderStageMask_Pixel);
	}

	// 컴퓨트 패스: CS 가 쓰는 슬롯이 그 패스의 레이아웃에 Compute 스테이지로 선언됐는지 (위 (2) 와 같은 규칙).
	for (uint32_t p = 0; p < ComputePass_Count; ++p)
	{
		const ComputePassInfo& pass = kComputePasses[p];
		std::vector<ShaderCompiler::ReflectedBinding> used;
		if (!ShaderCompiler::ReflectBindings(code[pass.cs], used)) continue;
		for (const auto& u : used)
		{
			bool found = false;
			for (uint32_t l = 0; l < pass.layoutCount && !found; ++l)
			{
				const BindingLayoutDesc& layout = m_layoutDescs[pass.layouts[l]];
				for (uint32_t i = 0; i < layout.slotCount; ++i)
				{
					const BindingSlot& slot = layout.slots[i];
					if (slot.type == u.type && slot.reg == u.reg && (slot.stageMask & ShaderStageMask_Compute))
					{
						found = true;
						break;
					}
				}
			}
			const char typeName = RegisterClassOf(u.type);
			if (!found)
			{
				Log::Error("바인딩 대조: [%s] %s가 %c%u ('%s')를 쓰지만 이 패스의 BindingLayout에 Compute 스테이지로 선언되지 않음.", pass.name, kShaders[pass.cs].name, typeName, u.reg, u.name.c_str());
				ok = false;
			}
			else
			{
				Log::Info("바인딩 대조: [%s] %s %c%u ('%s') OK", pass.name, kShaders[pass.cs].name, typeName, u.reg, u.name.c_str());
			}
		}
	}
	return ok;
}

bool Renderer::ReloadShaders()
{
	if (m_device == nullptr) return false;

	Log::Info("셰이더 핫리로드 시작…");
	if (!LoadShaders(true))
	{
		Log::Warn("셰이더 핫리로드 실패. 이전 셰이더를 유지한다.");
		// 실패해도 수정 시각은 갱신해 같은 파일로 계속 재시도하지 않게 함. 파일을 다시 저장하면 재시도함.
		for (WatchedFile& w : m_watched)
		{
			w.writeTime = ShaderCompiler::GetLastWriteTime(w.path);
		}
		return false;
	}

	// 새 셰이더 핸들로 PSO를 다시 만들어야 함. Desc 를 새 핸들로 다시 채우고 캐시를 비우면 이전 세대 핸들은
	// 무효가 되고, 다음 프레임의 Get*Pipeline 이 새 PSO를 만듦.
	BuildPassDescs();
	m_device->InvalidatePipelines();
	++m_stats.shaderReloads;
	Log::Info("셰이더 핫리로드 완료 (%u회째). PSO 캐시 재생성.", m_stats.shaderReloads);
	return true;
}

void Renderer::CheckHotReload(float totalTime)
{
#if defined(_DEBUG)
	if (totalTime < m_nextWatchTime) return;
	m_nextWatchTime = totalTime + kHotReloadPollInterval;

	bool changed = false;
	for (const WatchedFile& w : m_watched)
	{
		const uint64_t now = ShaderCompiler::GetLastWriteTime(w.path);
		if (now != 0 && now != w.writeTime)
		{
			changed = true;
			break;
		}
	}
	if (changed)
	{
		ReloadShaders();
	}
#else
	(void)totalTime;
#endif
}

void Renderer::Shutdown()
{
	if (m_device == nullptr) return;

	DestroyUIResources();   // 텍스처 셋이 텍스처를 가리키므로 텍스처 캐시보다 먼저

	for (auto& entry : m_gpuMeshes)
	{
		m_device->DestroyBuffer(entry.second.vertexBuffer);
		m_device->DestroyBuffer(entry.second.indexBuffer);
	}
	m_gpuMeshes.clear();
	for (auto& entry : m_gpuMaterials)
	{
		m_device->DestroyResourceSet(entry.second.set);
		m_device->DestroyBuffer(entry.second.constants);
	}
	m_gpuMaterials.clear();
	m_drawList.clear();
	m_objects.clear();
	m_gpuLights.clear();
	m_lightCapacity = 0;
	m_textures.Shutdown();
	m_environment = m_boundEnvironment = EnvironmentBinding{};
	DestroyViewTarget(m_mainView);
	DestroyViewTarget(m_previewView);
	DestroyStageThumbs();
	m_sceneWidth = m_sceneHeight = 0;
	m_outputWidth = m_outputHeight = 0;
	m_previewCamera = nullptr;
	DestroyRainResources();   // 비 차폐 맵·비 셋 (RendererRain.cpp)
	m_device->DestroyTexture(m_shadowMap);
	m_device->DestroyTexture(m_csmMap);
	m_device->DestroySampler(m_shadowSampler);
	m_shadowMap = TextureHandle{};
	m_csmMap = TextureHandle{};
	m_shadowSampler = SamplerHandle{};
	for (SamplerHandle& sampler : m_samplers)
	{
		m_device->DestroySampler(sampler);
		sampler = SamplerHandle{};
	}

	m_device->DestroyResourceSet(m_frameSet);
	m_device->DestroyResourceSet(m_objectSet);
	DestroyGpuEmitters();
	DestroyGpuClusterResources();
	m_device->DestroyResourceSet(m_blurWeightsSet);
	m_frameSet = m_objectSet = m_blurWeightsSet = ResourceSetHandle{};
	for (BindingLayoutHandle& layout : m_layouts)
	{
		m_device->DestroyBindingLayout(layout);
		layout = BindingLayoutHandle{};
	}
	for (BufferHandle* buffer : { &m_perFrameCB, &m_perObjectCB, &m_skinCB, &m_prevSkinCB, &m_oceanCB, &m_rainCB, &m_debugLineVB, &m_lightCB, &m_lightBuffer, &m_postParamsCB, &m_shadowPassCB, &m_shadowsCB, &m_blurWeights,
								  &m_ssaoCB, &m_clusterGridBuffer, &m_clusterIndexBuffer })
	{
		m_device->DestroyBuffer(*buffer);
		*buffer = BufferHandle{};
	}
	// PSO는 캐시가 소유함. 셰이더 핸들을 지워도 PSO는 자기 참조로 셰이더 객체를 붙들고 있음.
	for (ShaderHandle& shader : m_shaders)
	{
		m_device->DestroyShader(shader);
		shader = ShaderHandle{};
	}
	m_mainPipelines.clear();
	m_shadowPipelines.clear();
	m_gbufferPipelines.clear();
	m_clusterIndexCapacity = 0;
	m_ssaoKernel.clear();
	m_device = nullptr;
}

size_t Renderer::GetPipelineCount() const
{
	return m_device ? m_device->GetPipelineCount() : 0;
}

void Renderer::InvalidateMesh(const Mesh* mesh)
{
	auto found = m_gpuMeshes.find(mesh);
	if (found == m_gpuMeshes.end()) return;
	m_device->DestroyBuffer(found->second.vertexBuffer);
	m_device->DestroyBuffer(found->second.indexBuffer);
	m_gpuMeshes.erase(found);
}

void Renderer::InvalidateMaterial(const Material* material)
{
	auto found = m_gpuMaterials.find(material);
	if (found == m_gpuMaterials.end()) return;
	m_device->DestroyResourceSet(found->second.set);
	m_device->DestroyBuffer(found->second.constants);
	m_gpuMaterials.erase(found);
}

void Renderer::ResetTemporalHistory()
{
	// 뷰: 지난 카메라 행렬·TAA 히스토리를 버림 (다음 프레임은 이전 = 이번 → 속도 0, TAA 는 이번 프레임을 그대로 씀).
	for (ViewTarget* view : { &m_mainView, &m_previewView })
	{
		view->temporal.hasPrev = false;
		view->temporal.historyValid = false;
	}
	// 오브젝트: 지난 World·팔레트를 버림 (id 가 같아도 씬이 바뀌었으면 다른 물체일 수 있음).
	for (MotionHistory& history : m_motion)
	{
		history.objects.clear();
		history.palettes.clear();
	}
	m_oceanHasPrev = false;   // 바다: 다음 프레임은 지난 위상 = 이번 위상 (속도 0)
	m_rainSnapWetness = true; // 비: 씬 불러오기·재생 시작/정지 직후는 젖음을 바로 목표값으로 (천천히 젖어 가는 연출은 같은 씬 안에서만)
}

void Renderer::InvalidateScene(const Scene& scene, bool releaseTextures)
{
	ResetTemporalHistory();   // 씬 불러오기·새 씬·재생 정지(스냅샷 복원)는 모두 여기를 지남
	for (const auto& mesh : scene.GetMeshes())
	{
		InvalidateMesh(mesh.get());
	}
	for (const auto& material : scene.GetMaterials())
	{
		InvalidateMaterial(material.get());
	}
	// 드로우 목록은 방금 지운 GPU 메시·재질을 가리킴. 다음 Render 가 다시 만들지만, 그 전에 누가 읽지 않게 비움.
	m_drawList.clear();
	m_objects.clear();
	if (releaseTextures)
	{
		// 내장 텍스처("builtin:")는 남김 — 흰색·평평한 노멀은 프레임/기본 재질이 계속 씀. 세대 번호가 올라가
		// 남아 있는 재질 셋(기본 재질 등)도 다음 프레임에 텍스처를 다시 찾음.
		m_textures.ReleaseLoaded();
		// 프레임 셋의 t9~t12 가 지운 텍스처를 가리킬 수 있음. 바인딩 기록을 비우면 다음 프레임에 셋을
		// 반드시 다시 만듦.
		m_boundEnvironment = EnvironmentBinding{};
	}
}

// ------------------------------------------------------------------ 프레임

void Renderer::Render(const Scene& scene, const Camera& camera, float totalTime, const DebugDraw& debugDraw)
{
	m_uiReady = false;   // UploadUI 가 이번 프레임에 올렸을 때만 그림 (일찍 끝나는 프레임에 지난 프레임 버퍼를 묶지 않게)
	if (m_device == nullptr) return;

	CheckHotReload(totalTime);
	// 그릴 타깃(HDR·깊이)이 있어야 함. 에디터는 SetSceneTarget 이 이미 만들었고, 게임 모드는 출력 크기에 맞춰
	// 여기서 만듦. 만들지 못했으면 이번 프레임은 건너뜀.
	if (!IsOffscreen())
	{
		EnsureGameTarget();
	}
	if (!m_mainView.IsValid()) return;
	// 단계별 썸네일. 창이 요청한 프레임에만 (요청은 한 번 쓰면 지움 — 창을 닫으면 다음 프레임부터 멈춤).
	m_postCapture = m_postCaptureRequested && IsOffscreen();
	m_postCaptureRequested = false;
	for (PostStageThumb& thumb : m_stageThumbs)
	{
		thumb.active = false;
	}
	if (m_postCapture)
	{
		EnsureStageThumbs(m_mainView.width, m_mainView.height);
	}
	++m_frameNumber;
	const uint32_t reloads = m_stats.shaderReloads;
	const uint32_t lastPasses = m_stats.renderPasses;   // 패스·배리어 수는 UI 패스까지 센 지난 프레임 값 (EndUIPass 가 채움)
	const uint32_t lastBarriers = m_stats.barriers;
	m_stats = Stats{};
	m_stats.shaderReloads = reloads;
	m_stats.renderPasses = lastPasses;
	m_stats.barriers = lastBarriers;

	RHI::CommandList& cmd = m_device->GetCommandList();

	m_shadowSettings = scene.environment.shadow;   // 씬 파일에 저장된 그림자 값
	m_ssaoSettings = scene.environment.ssao;       // 씬 파일에 저장된 SSAO 값 (디퍼드 프레임에서만 씀)
	if (m_settings.ssaoOverride >= 0)
	{
		m_ssaoSettings.enabled = m_settings.ssaoOverride != 0;   // --ssao= 실행 인자
	}

	// ---- 조명: 씬의 Light → GPU 배치. BuildShadowViews 가 shadowIndex 를 채우므로 올리기(UploadLights) 전에 만듦 ----
	// 방향광을 앞으로 모음 (안정 정렬 — 같은 종류끼리는 씬 순서). 클러스터드 경로가 "앞쪽 directionalCount 개만 전부 돌기"
	// 를 하려고. CPU 구간 (Profiler 패널): Prepare = 조명·그림자 뷰·클러스터·환경·상수 준비, BuildDrawList, 패스의 드로우 제출
	Profiler::BeginCpu("Prepare");
	BuildGpuLights(scene);

	// ---- 3a: 조명 경계 구 → 메인 카메라 절두체 컬링 → 그림자 후보 점수 순서 ----
	// 컬링 고정 중이고 lights 항목이 켜져 있으면 고정된 사본 기준 (컬링된 지역광은 클러스터에서도 빠짐 — CullLights).
	CullLights(m_cullFrozen && m_cullFreezeParts.lights ? m_cullFreezeCamera : camera);

	// ---- 그림자 뷰: 조명마다 아틀라스 타일을 배정하고 shadowIndex 를 채움 → b6(+b5) → b0 (shadowParams.w 가 이 결과를 씀) ----
	// 3a 가 켜져 있으면 지역광은 점수 순서로 타일을 받음 (화면 밖 조명은 받지 않음).
	BuildShadowViews(camera, m_gpuLights);
	m_shadowEnabledThisFrame = m_shadowViewCount > 0;
	UploadShadowConstants();

	// ---- 이번 프레임에 필요한 GPU 리소스를 패스 시작 전에 전부 만듦 ----
	// 환경 큐브(ResolveEnvironment), 메시 버퍼·재질 셋·재질 텍스처(BuildDrawList). 새로 만드는 것이 있으면 그
	// 초기 데이터 복사를 묶어 한 번에 제출함 (D3D12: 리소스마다 GPU 를 기다리지 않음). 드로우 루프 안에서는
	// 아무것도 만들지 않음.
	m_device->BeginResourceUploads();

	// 환경 (스카이박스 t9, IBL t10~t12, 노출·톤매핑). b0 의 environmentParams·outputParams 가 이 값을 씀
	ResolveEnvironment(scene.environment);

	// ---- b0 PerFrame (메인 뷰) ----
	// 이번 프레임 메인 뷰가 디퍼드인가·SSAO 결과가 있는가 — b0 aoParams 가 이것을 씀.
	const bool mainSsao = UseDeferred(m_mainView) && m_ssaoSettings.enabled && m_mainView.HasSsao();
	// 비: 첫 번째 Rain → 차폐 맵 위치·젖음 보간·빗방울 수 (b0 의 rain* 칸이 이 값을 씀). 차폐 맵 텍스처도 여기서 맞춤 (업로드 구간 안)
	PrepareRain(scene, camera, totalTime);
	UploadPerFrameConstants(camera, m_mainView, totalTime, mainSsao);

	// ---- b2 Lights + t15 조명 배열 ----
	UploadLights(scene);

	// ---- 3b: 클러스터 비닝. 메인과 (있으면) 미리보기를 지금 둘 다 만들어 t31 용량을 한 번에 맞춤 — 미리보기 패스 직전에
	// 버퍼를 다시 만들면 이미 기록된 메인 패스가 꽂은 프레임 셋이 옛 버퍼를 가리키게 됨. 올리기는 뷰 패스마다 (t30·t31 은
	// Dynamic 이라 뷰마다 새 메모리) ----
	const bool previewActive = m_previewCamera != nullptr && m_previewView.IsValid() && m_previewView.color.IsValid();
	// 미리보기를 그리지 않은 프레임이 끼면 그 뒤 첫 프레임의 "지난 프레임" 은 여러 프레임 전이라 쓸 수 없음. 같은 카메라를 다시 골라도
	// 리셋되게 카메라 기억을 지움 (UploadPerFrameConstants 가 lastCamera 가 다르면 지난 행렬·TAA 히스토리를 버림).
	if (!previewActive)
	{
		m_previewView.temporal.lastCamera = nullptr;
	}
	// 미리보기 대상 오브젝트가 바뀜 (카메라 A 를 보다가 B 를 선택): Camera 사본의 주소는 같으므로 id 로 알아내 같은 방법으로 리셋
	if (previewActive && m_previewSourceId != m_previewLastSourceId)
	{
		m_previewView.temporal.lastCamera = nullptr;
	}
	m_previewLastSourceId = previewActive ? m_previewSourceId : 0;
	// GPU 비닝 (docs/plan/gpu-cluster-binning-record.html): 여기서는 입력(t7 경계 구, 뷰마다 b13)만 올리고, 비닝 자체는 렌더
	// 패스 밖에서 뷰 패스 직전에 디스패치함 (DispatchClusterBinning). 통계는 몇 프레임 (≥ kFrameCount) 전 것을 읽음.
	m_clusterGpuThisFrame = PrepareGpuClusters();
	m_stats.clusterGpu = m_clusterGpuThisFrame;
	{
		ProfileScope cpuScope("BuildClusters");
		if (m_clusterGpuThisFrame)
		{
			ReadGpuClusterStats();
			UploadLightBounds();
			UploadClusterBinConstants(camera, 0);
			if (previewActive)
			{
				UploadClusterBinConstants(*m_previewCamera, 1);
			}
		}
		else
		{
			BuildClusters(camera, m_mainView.width, m_mainView.height, 0);
			m_clusterBuilt[1] = false;
			if (previewActive)
			{
				BuildClusters(*m_previewCamera, m_previewView.width, m_previewView.height, 1);
			}
			EnsureClusterCapacity();
			UploadClusters(0);
		}
	}

	// ---- b12 SSAO (프레임에 한 번 — 두 뷰가 같은 값) ----
	UploadSsaoConstants(m_ssaoSettings);
	Profiler::EndCpu();   // Prepare

	{
		ProfileScope cpuScope("BuildDrawList");
		BuildDrawList(scene);
		m_stats.drawItems = static_cast<uint32_t>(m_drawList.size());
	}
	UploadDebugLines(debugDraw);                 // 디버그 선(DebugDraw 가 모은 것)을 동적 정점 버퍼에 올림. 프레임마다 한 번은 써야 함
	UpdateParticleEmitters(totalTime, camera);   // BuildDrawList 가 모은 이미터의 버퍼·셋을 맞추고 b8 을 올림 (업로드 구간 안)
	UploadOceanConstants(totalTime);             // b9: BuildDrawList 가 모은 바다의 파도·위상 (없으면 파도 0 개). 그림자 패스를 포함한 모든 드로우 전
	UploadRainConstants();                       // b8 비 셋 (비가 있을 때만 — 비 셋은 비 패스만 묶음)
	UploadUI(scene);                             // 게임 UI: 텍스처 셋 + 정점·인덱스·캔버스 행렬 (목록이 비면 아무것도 안 올림)

	m_device->EndResourceUploads();

	// 환경 텍스처를 프레임 셋 t9~t12 에 꽂음. 없는 것도 슬롯을 비우지 않고 검은 큐브·흰 LUT 를 꽂음
	// (D3D12 디스크립터 테이블에는 유효한 뷰가 있어야 함. 셰이더는 environmentParams·outputParams 의 플래그로
	// 쓸지 정함).
	UpdateFrameSet();

	// 프레임 셋(b0, b2, t8, s4, t9, s5, t10~t12)과 오브젝트 셋(b1)을 바인딩함. 그림자 패스는 t8(그림자 맵)을
	// DSV 로 쓰므로 BeginRenderPass 가 t8 SRV 바인딩을 해제함. 씬 패스에서 셋을 다시 바인딩해 t8 을 복구함.
	// (파티클 시뮬레이션은 메인 씬 패스 안, 불투명 패스와 파티클 패스 사이에서 함 — RenderScenePass.)
	// GPU 비닝 (메인 뷰). 렌더 패스 밖이어야 하고, 프레임 셋을 꽂기 전에 t30·t31 을 ShaderResource 로 돌려 둠
	// (D3D11: UnorderedAccess 로 바꿀 때 백엔드가 그 버퍼의 SRV 바인딩을 풂).
	if (m_clusterGpuThisFrame)
	{
		GpuProfileScope gpuScope(cmd, "ClusterBin");
		DispatchClusterBinning(cmd, 0);
	}
	if (m_frameRain.active && m_rainMap.IsValid())
	{
		cmd.Barrier(m_rainMap, ResourceState::ShaderResource);   // 프레임 셋 t24 (새로 만든 맵·지난 프레임 끝 상태 그대로면 아무 일도 없음)
	}
	cmd.SetResourceSet(kSetFrame, m_frameSet);
	cmd.SetResourceSet(kSetObject, m_objectSet);

	if (m_shadowEnabledThisFrame)
	{
		GpuProfileScope gpuScope(cmd, "ShadowPass");
		ProfileScope cpuScope("ShadowPass");
		RenderShadowPass(cmd);
	}
	else
	{
		// 그림자 패스가 없는 프레임에도 프레임 셋은 t8 / t14 를 SRV 로 가리킴. 상태를 맞춰 둠 (이미 그 상태면 아무 일도 없음).
		cmd.Barrier(m_shadowMap, ResourceState::ShaderResource);
		cmd.Barrier(m_csmMap, ResourceState::ShaderResource);
	}
	if (m_frameRain.active)
	{
		// 비 차폐 맵 (docs/plan/rain-plan.html): 위에서 내려다본 깊이. 끝나면 맵은 ShaderResource (프레임 셋 t24).
		GpuProfileScope gpuScope(cmd, "RainOcclusion");
		ProfileScope cpuScope("RainOcclusion");
		RenderRainOcclusionPass(cmd);
	}
	{
		GpuProfileScope gpuScope(cmd, "MainPass");
		ProfileScope cpuScope("MainPass");
		RenderScenePass(cmd, scene, m_mainView, camera, "MainPass", 0);
	}
	// 모든 후처리 셋이 t20 에 가중치 버퍼를 꽂음. 버퍼 상태는 프레임마다 Common 으로 돌아가므로 첫 후처리 패스 전에
	// ShaderResource 로 둠 (블러가 꺼진 프레임에도 — 톤 패스가 같은 셋 레이아웃을 바인딩함).
	cmd.Barrier(m_blurWeights, ResourceState::ShaderResource);
	// HDR 구간 후처리 (블러 → 블룸). 결과는 hdr 에 제자리로 남아 톤 패스가 그대로 읽음.
	ApplyHdrEffects(cmd, m_mainView, m_postCapture);
	{
		// HDR: HDR 씬 → 노출·ACES·sRGB → 최종 출력. 에디터는 씬 뷰 텍스처, 게임은 백버퍼.
		// 미리보기 패스가 b0 를 다른 카메라로 덮어쓰기 전에 함 (톤 패스는 b0 의 출력 파라미터만 읽지만,
		// 카메라에 의존하지 않게 순서를 둠).
		GpuProfileScope gpuScope(cmd, "TonemapPass");
		// 에디터: 끝나면 ShaderResource (ImGui 가 샘플링함). 게임: 백버퍼를 RenderTarget 으로 둠 — UI 패스가
		// 이어서 그 위에 그림.
		const TextureHandle output = IsOffscreen() ? m_mainView.color : m_device->GetBackBuffer();
		const ResourceState after = IsOffscreen() ? ResourceState::ShaderResource : ResourceState::RenderTarget;
		RenderTonemapPass(cmd, m_mainView.hdrSet, output, after, "TonemapPass", m_mainView.width, m_mainView.height, m_mainView.width, m_mainView.height);
	}
	{
		// 게임 UI (Screen Space Overlay). 톤 패스가 남긴 상태에서 RenderTarget 으로 바꿔 덧그리고 같은 상태로 돌려놓음 — 에디터는 ImGui 가
		// 샘플링하므로 ShaderResource, 게임은 다음 BeginUIPass 가 Load 하므로 RenderTarget. 명령이 없으면 아무것도 하지 않음.
		const TextureHandle output = IsOffscreen() ? m_mainView.color : m_device->GetBackBuffer();
		const ResourceState after = IsOffscreen() ? ResourceState::ShaderResource : ResourceState::RenderTarget;
		RenderCanvasPass(cmd, 0, output, m_mainView.depth, after, m_mainView.width, m_mainView.height);
	}
	if (previewActive)
	{
		// 선택한 카메라 오브젝트의 시점. b0 와 클러스터(t30·t31)만 그 카메라로 바꿔 같은 드로우 목록을 작은 타깃에 다시 그림
		// (컬링은 그 카메라의 절두체로 다시 함). 그림자 뷰·3a 컬링은 메인 카메라 기준 그대로.
		GpuProfileScope gpuScope(cmd, "PreviewPass");
		const bool previewSsao = UseDeferred(m_previewView) && m_ssaoSettings.enabled && m_previewView.HasSsao();
		UploadPerFrameConstants(*m_previewCamera, m_previewView, totalTime, previewSsao);
		if (m_clusterGpuThisFrame)
		{
			// 같은 GPU 버퍼에 미리보기 카메라로 다시 비닝 (t30·t31: ShaderResource → UnorderedAccess → ShaderResource). 메인 패스의
			// 그리기는 명령 순서상 이미 끝났음 — 배리어가 그 순서를 지킴. 프레임 셋은 RenderScenePass 의 각 패스가 다시 꽂음.
			GpuProfileScope binScope(cmd, "PreviewClusterBin");
			DispatchClusterBinning(cmd, 1);
		}
		else
		{
			UploadClusters(1);
		}
		RenderScenePass(cmd, scene, m_previewView, *m_previewCamera, "PreviewPass", 1);
		ApplyHdrEffects(cmd, m_previewView, false);
		RenderTonemapPass(cmd, m_previewView.hdrSet, m_previewView.color, ResourceState::ShaderResource, "PreviewTonemapPass", m_previewView.width, m_previewView.height, m_previewView.width, m_previewView.height);
		RenderCanvasPass(cmd, 1, m_previewView.color, m_previewView.depth, ResourceState::ShaderResource, m_previewView.width, m_previewView.height);
	}
}

void Renderer::BuildGpuLights(const Scene& scene)
{
	const std::vector<Light>& sceneLights = scene.GetLights();
	m_gpuLights.clear();   // 용량은 남음 — 프레임마다 다시 할당하지 않음
	m_gpuLights.reserve(sceneLights.size());
	m_lightSceneIndex.clear();
	// 두 번 돎: 방향광 먼저, 그다음 지역광. 같은 종류 안에서는 씬 순서 그대로 (안정 정렬과 같음) — "첫 번째 castShadows
	// 방향광이 CSM 을 받는다"는 규칙이 정렬 전과 같은 조명을 가리킴. m_lightSceneIndex[i] = 정렬 뒤 i 번째의 씬 번호.
	for (uint32_t pass = 0; pass < 2; ++pass)
	{
		for (uint32_t sceneIndex = 0; sceneIndex < static_cast<uint32_t>(sceneLights.size()); ++sceneIndex)
		{
			const Light& light = sceneLights[sceneIndex];
			const bool directional = light.type == LightType::Directional;
			if (directional != (pass == 0)) continue;
			m_lightSceneIndex.push_back(sceneIndex);
			GpuLight gpu = {};
			gpu.position = light.position;
			gpu.intensity = light.intensity;
			gpu.range = light.range;
			gpu.color = light.color;
			gpu.type = static_cast<uint32_t>(light.type);
			gpu.attenuation = light.attenuation;
			gpu.innerConeCos = light.innerConeCos;
			gpu.outerConeCos = light.outerConeCos;
			gpu.castShadows = light.castShadows ? 1u : 0u;
			gpu.shadowIndex = -1;   // BuildShadowViews 가 채움

			// 셰이더는 정규화된 방향을 기대함. 0벡터면 아래를 향하게 함.
			const XMVECTOR direction = XMLoadFloat3(&light.direction);
			if (XMVectorGetX(XMVector3LengthSq(direction)) > 0.000001f)
			{
				XMStoreFloat3(&gpu.direction, XMVector3Normalize(direction));
			}
			else
			{
				gpu.direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
			}
			m_gpuLights.push_back(gpu);
		}
	}
}

bool Renderer::UploadLights(const Scene& scene)
{
	const uint32_t count = static_cast<uint32_t>(m_gpuLights.size());
	if (count > m_lightCapacity)
	{
		// 자리가 모자람: 두 배씩 늘려 다시 만듦. 프레임 셋(t15)이 옛 버퍼를 가리키므로 바인딩 기록을 비워
		// 다음 UpdateFrameSet 이 셋을 다시 만들게 함. 옛 버퍼는 D3D12 에서 지연 해제됨.
		uint32_t capacity = m_lightCapacity > 0 ? m_lightCapacity : kInitialLightCapacity;
		while (capacity < count)
		{
			capacity *= 2;
		}
		const BufferHandle grown = m_device->CreateBuffer(BufferDesc{ .size = capacity * static_cast<uint32_t>(sizeof(GpuLight)), .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_ShaderResource, .stride = sizeof(GpuLight), .debugName = "SB_Lights" });
		if (grown.IsValid())
		{
			m_device->DestroyBuffer(m_lightBuffer);
			m_lightBuffer = grown;
			m_lightCapacity = capacity;
			m_boundEnvironment = EnvironmentBinding{};
			Log::Info("조명 버퍼 확장: %u개 (%u바이트)", capacity, capacity * static_cast<uint32_t>(sizeof(GpuLight)));
		}
		else
		{
			static bool warned = false;
			if (!warned)
			{
				warned = true;
				Log::Error("조명 버퍼 확장 실패 (%u개). 앞의 %u개만 그림.", count, m_lightCapacity);
			}
		}
	}
	const uint32_t uploaded = (std::min)(count, m_lightCapacity);

	// 조명이 0개여도 한 칸은 올림 — D3D12 의 Dynamic 버퍼는 프레임마다 갱신돼야 함 (셰이더는 lightCount 만큼만 읽음).
	static const GpuLight kNoLight = {};
	m_device->UpdateBuffer(m_lightBuffer, uploaded > 0 ? m_gpuLights.data() : &kNoLight, (std::max)(uploaded, 1u) * static_cast<uint32_t>(sizeof(GpuLight)));

	LightConstants constants = {};
	constants.ambientColor = scene.ambientColor;
	constants.lightCount = uploaded;
	// BuildGpuLights 가 방향광을 앞으로 모았으므로 앞에서부터 방향광인 동안 셈.
	uint32_t directionalCount = 0;
	while (directionalCount < uploaded && m_gpuLights[directionalCount].type == static_cast<uint32_t>(LightType::Directional))
	{
		++directionalCount;
	}
	constants.directionalCount = directionalCount;
	m_device->UpdateBuffer(m_lightCB, &constants, sizeof(constants));
	return uploaded == count;
}

void Renderer::BuildDrawList(const Scene& scene)
{
	m_drawList.clear();
	m_objects.clear();

	// 속도 버퍼: 지난 프레임 오브젝트 상태(prev)를 읽고 이번 것(current)을 새로 채움. 두 벌을 맞바꾸기만 함.
	m_motionCurrent ^= 1u;
	MotionHistory& motionNow = m_motion[m_motionCurrent];
	const MotionHistory& motionPrev = m_motion[m_motionCurrent ^ 1u];
	motionNow.objects.clear();
	motionNow.palettes.clear();
	// b13 PrevSkin 도 b7 처럼 오브젝트 셋에 항상 묶임 — 스키닝 드로우가 없는 프레임에도 한 번 씀 (D3D12 Dynamic CB 규칙)
	XMStoreFloat4x4(&m_skinScratch[0], XMMatrixIdentity());
	m_device->UpdateBuffer(m_prevSkinCB, m_skinScratch.data(), static_cast<uint32_t>(sizeof(XMFLOAT4X4)));

	// b7 Skin 은 오브젝트 셋에 항상 묶여 있음. D3D12 의 Dynamic 상수버퍼는 프레임마다 한 번은 써야 하므로(안 쓰면
	// ApplyBindings 가 에러를 한 번 남기고 옛 링 조각이나 빈 루트 CBV 로 그대로 그림) 스키닝 드로우가 없는 프레임에도 단위행렬 하나로 채워 둠. 스키닝 드로우는 그리기 직전에 다시 씀.
	XMStoreFloat4x4(&m_skinScratch[0], XMMatrixIdentity());
	m_device->UpdateBuffer(m_skinCB, m_skinScratch.data(), static_cast<uint32_t>(sizeof(XMFLOAT4X4)));

	m_frameEmitters.clear();
	m_frameOcean.active = false;
	const auto addObject = [&](const GameObject& object, const XMMATRIX& world, const Highlight* highlight, const Dissolve* dissolve)
	{
		CollectEmitter(object, world);   // 메시가 없는 오브젝트도 이미터일 수 있으므로 아래 return 보다 먼저
		const Mesh* mesh = object.GetMesh();
		if (mesh == nullptr || !mesh->IsValid()) return;
		const GpuMesh* gpuMesh = GetOrCreateGpuMesh(*mesh);
		if (gpuMesh == nullptr) return;

		// 월드 행렬은 호출한 쪽이 계층을 따라 내려오며 구해 줌. 여기서는 역행렬·경계 구를 만들어 둠 —
		// 그림자 뷰 N 개 + 메인 + 미리보기 패스가 모두 이 결과를 씀.
		ObjectData data;
		XMStoreFloat4x4(&data.world, XMMatrixTranspose(world));
		// 노멀 변환 행렬은 (W⁻¹)ᵀ. 전치해서 올려야 하므로 ((W⁻¹)ᵀ)ᵀ = W⁻¹ 를 그대로 저장함.
		XMStoreFloat4x4(&data.worldInvTranspose, XMMatrixInverse(nullptr, world));
		// 경계 구: 중심 = 메시 경계 중심을 월드 행렬로 변환, 반지름 = 메시 경계 반지름 × 월드 행렬 세 축 길이의
		// 최댓값 (비균등 스케일 대비).
		const XMFLOAT3 localCenter = mesh->GetBounds().Center();
		XMFLOAT3 center;
		XMStoreFloat3(&center, XMVector3TransformCoord(XMLoadFloat3(&localCenter), world));
		const float scale = (std::max)({ XMVectorGetX(XMVector3Length(world.r[0])), XMVectorGetX(XMVector3Length(world.r[1])),
										 XMVectorGetX(XMVector3Length(world.r[2])) });
		data.sphere = XMFLOAT4(center.x, center.y, center.z, mesh->GetBounds().Radius() * scale);
		if (mesh->IsSkinned())
		{
			// 스키닝 메시의 실제 경계는 포즈마다 바뀜 (바인드 포즈 경계는 휘어진 팔을 못 덮음). 정확한 경계를 구하기 전까지
			// 컬링하지 않음 — 큰 반지름을 줘서 모든 절두체(메인·그림자)를 통과시킴.
			data.sphere.w = 1.0e9f;
			data.skinned = true;
			data.skinPalette = &object.GetSkinPalette();
		}
		// 바다 (첫 번째 하나): 경계 구에 감쇠 전 최대 변위를 더해 화면 가장자리에서 파도 마루가 잘리지 않게 함. 스킨 메시는 바다로 그리지 않음.
		const bool ocean = !data.skinned && CollectOcean(object, world);
		if (ocean)
		{
			data.sphere.w += Ocean::MaxDisplacement(m_frameOcean.waves, m_frameOcean.waveCount);
		}
		// 오브젝트별 강조 (b1). 값의 범위는 여기서 자름: 섞는 양 [0, 1], 세기 ≥ 0, 가장자리 지수 ≥ 0.1 (셰이더 pow 의 0 지수 방지).
		if (highlight != nullptr)
		{
			const float tintAmount = std::clamp(highlight->tintAmount, 0.0f, 1.0f);
			const float flash = (std::max)(highlight->flashIntensity, 0.0f);
			const float rim = (std::max)(highlight->rimIntensity, 0.0f);
			data.highlightTint = XMFLOAT4(highlight->tintColor.x, highlight->tintColor.y, highlight->tintColor.z, tintAmount);
			data.highlightFlash = XMFLOAT4(highlight->flashColor.x * flash, highlight->flashColor.y * flash, highlight->flashColor.z * flash, 0.0f);
			data.highlightRim = XMFLOAT4(highlight->rimColor.x * rim, highlight->rimColor.y * rim, highlight->rimColor.z * rim,
										 (std::max)(highlight->rimPower, 0.1f));
		}
		// 디졸브 (b1). 진행도 [0, 1], 경계 폭·세기 ≥ 0. 진행도 0 이면 셰이더가 아무것도 하지 않고 그림자도 빠른 경로 그대로.
		bool dissolving = false;
		if (dissolve != nullptr)
		{
			const float amount = std::clamp(dissolve->amount, 0.0f, 1.0f);
			const float edge = (std::max)(dissolve->edgeIntensity, 0.0f);
			data.dissolveEdge = XMFLOAT4(dissolve->edgeColor.x * edge, dissolve->edgeColor.y * edge, dissolve->edgeColor.z * edge, amount);
			data.highlightFlash.w = (std::max)(dissolve->edgeWidth, 0.0f);   // b1 을 256 B 로 맞추려고 번쩍임의 남는 칸에 둠 (ShaderConstants.h)
			dissolving = amount > 0.0f;
		}
		// 속도 버퍼: 지난 프레임의 이 오브젝트 (id 는 씬 안에서 고유). 없으면 이번 값 = 속도 0.
		data.prevWorld = data.world;
		const auto prevFound = motionPrev.objects.find(object.GetId());
		if (prevFound != motionPrev.objects.end())
		{
			data.prevWorld = prevFound->second.world;
			data.prevPaletteOffset = prevFound->second.paletteOffset;
			data.prevPaletteCount = prevFound->second.paletteCount;
		}
		PrevObject now{ .world = data.world };
		if (data.skinned && !data.skinPalette->empty())
		{
			now.paletteOffset = static_cast<uint32_t>(motionNow.palettes.size());
			now.paletteCount = static_cast<uint32_t>((std::min)(data.skinPalette->size(), static_cast<size_t>(kMaxSkinJoints)));
			motionNow.palettes.insert(motionNow.palettes.end(), data.skinPalette->begin(), data.skinPalette->begin() + now.paletteCount);
		}
		motionNow.objects[object.GetId()] = now;
		const uint32_t objectIndex = static_cast<uint32_t>(m_objects.size());
		m_objects.push_back(data);

		// 월드 행렬식이 음수(scale -1 미러)면 화면에서 삼각형 감기 순서가 뒤집혀 뒷면 컬링이 앞면을 자름.
		// 오브젝트마다 한 번만 판정.
		const bool mirrored = XMVectorGetX(XMMatrixDeterminant(world)) < 0.0f;

		// 서브메시마다 DrawItem 하나. 재질은 슬롯 → 오브젝트 기본 → Renderer 기본 순으로 고름.
		for (const Submesh& submesh : mesh->GetSubmeshes())
		{
			if (submesh.indexCount == 0) continue;
			const Material* material = object.GetMaterialForSlot(submesh.materialSlot);
			if (material == nullptr)
			{
				material = &m_defaultMaterial;
			}

			DrawItem item;
			item.pipeline = GetPipelineFor(*material, mesh->GetVertexFormat(), mirrored, ocean);
			if (!item.pipeline.IsValid()) continue;
			item.gpuMaterial = GetOrCreateGpuMaterial(*material, &scene);
			if (item.gpuMaterial == nullptr) continue;
			// 감마 검증 카드처럼 조명을 받지 않는 물체는 그림자도 만들지 않음 (빈 핸들 = 그림자 패스에서 건너뜀).
			// 블렌딩 재질(유리·반투명)도 그림자를 만들지 않음: 그림자 맵은 불투명한 차폐만 담고, 반투명은 알파가 없어 통째로 가림.
			item.blended = IsBlended(*material);
			// 바다도 그림자를 만들지 않음 (받기만 함): 넓고 거의 평평한 면이 그림자 맵에 들어가면 비용만 들고 자기 그림자 여드름이 생김.
			// 컷아웃 재질과 디졸브 중인 오브젝트는 알파 테스트 그림자 (PS 가 같은 알파·노이즈로 구멍을 냄). 나머지는 PS 없는 빠른 경로.
			item.shadowAlpha = material->alphaCutoff > 0.0f || dissolving;
			item.shadowPipeline = (material->unlit || item.blended || ocean) ? PipelineHandle{} : GetShadowPipelineFor(*material, mesh->GetVertexFormat(), mirrored, item.shadowAlpha);
			// 비 차폐 맵: 그림자를 만드는 항목과 같은 것 (바이어스 없는 변형). 비가 없는 프레임에는 만들지 않음 (PSO 생성을 늘리지 않게).
			if (m_frameRain.active && item.shadowPipeline.IsValid())
			{
				item.occluderPipeline = GetShadowPipelineFor(*material, mesh->GetVertexFormat(), mirrored, item.shadowAlpha, true);
			}
			// 디퍼드 (2단계): G-buffer 에 담을 수 있는 재질이면 G-buffer PSO 도 만들어 둠. 디퍼드를 꺼도 만들어 두는 이유: 토글할 때
			// 드로우 목록을 다시 만들지 않으려고 (PSO 는 캐시라 한 번만 생성). 실패하면 포워드로 그림.
			item.deferred = CanUseDeferred(*material);
			if (item.deferred)
			{
				item.gbufferPipeline = GetGBufferPipelineFor(*material, mesh->GetVertexFormat(), mirrored, ocean);
				item.deferred = item.gbufferPipeline.IsValid();
			}
			item.material = material;
			item.mesh = mesh;
			item.gpuMesh = gpuMesh;
			item.object = objectIndex;
			item.indexStart = submesh.indexStart;
			item.indexCount = submesh.indexCount;
			m_drawList.push_back(item);
		}
	};

	// 부모 → 자식 순으로 내려가며 월드 행렬을 오브젝트당 한 번씩만 구함. 예전에는 오브젝트마다
	// GetWorldMatrix 가 부모를 재귀로 올라가며 로컬 행렬을 처음부터 다시 만들어 깊이에 비례해 비쌌음.
	// m_objects 의 순서는 생성 순서라 부모가 자식보다 앞에 온다는 보장이 없으므로(SetParent 는 아무 둘 사이에서나
	// 가능), 순서에 기대지 않고 루트에서 children 을 따라감. 방문 순서는 아래 정렬이 지우므로 화면에 영향이 없음.
	size_t visited = 0;
	for (const auto& rootPtr : scene.GetObjects())
	{
		const GameObject& root = *rootPtr;   // 씬이 unique_ptr 로 소유함
		if (root.GetParent() != nullptr) continue;
		const XMMATRIX rootWorld = root.GetTransform().GetLocalMatrix();   // 루트는 로컬 = 월드
		const Highlight* rootHighlight = ResolveInherited<Highlight>(root, nullptr);
		const Dissolve* rootDissolve = ResolveInherited<Dissolve>(root, nullptr);
		addObject(root, rootWorld, rootHighlight, rootDissolve);
		++visited;
		if (root.GetChildren().empty()) continue;

		m_hierarchyStack.clear();
		m_hierarchyStack.push_back(HierarchyFrame{ &root, 0, {}, rootHighlight, rootDissolve });
		XMStoreFloat4x4(&m_hierarchyStack.back().world, rootWorld);
		while (!m_hierarchyStack.empty())
		{
			HierarchyFrame& frame = m_hierarchyStack.back();
			const auto& children = frame.node->GetChildren();
			if (frame.next >= children.size())
			{
				m_hierarchyStack.pop_back();
				continue;
			}
			const GameObject& child = *children[frame.next++];
			// 행벡터 관례: 자식 로컬을 먼저 적용하고 부모로 올라감 (GameObject::GetWorldMatrix 와 같은 곱셈 순서)
			const XMMATRIX world = child.GetTransform().GetLocalMatrix() * XMLoadFloat4x4(&frame.world);
			const Highlight* childHighlight = ResolveInherited<Highlight>(child, frame.highlight);
			const Dissolve* childDissolve = ResolveInherited<Dissolve>(child, frame.dissolve);
			addObject(child, world, childHighlight, childDissolve);
			++visited;
			if (!child.GetChildren().empty())
			{
				HierarchyFrame next{ &child, 0, {}, childHighlight, childDissolve };
				XMStoreFloat4x4(&next.world, world);
				m_hierarchyStack.push_back(next);   // frame 참조는 이 뒤로 쓰지 않음 (벡터가 늘어나며 무효가 될 수 있음)
			}
		}
	}
	// children 과 parent 가 어긋나면 일부 오브젝트가 조용히 안 그려짐. 한 번만 알림.
	static bool warnedMismatch = false;
	if (visited != scene.GetObjects().size() && !warnedMismatch)
	{
		warnedMismatch = true;
		Log::Error("BuildDrawList: 계층 순회가 %zu 개만 방문함 (씬 오브젝트 %zu 개). parent/children 불일치?", visited, scene.GetObjects().size());
	}

	// ---- 정렬: PSO → Material → Mesh → 인덱스 범위. 같은 상태가 연속되면 바인딩을 건너뛸 수 있음 ----
	std::sort(m_drawList.begin(), m_drawList.end(), [](const DrawItem& a, const DrawItem& b)
	{
		if (a.blended != b.blended) return !a.blended;   // 불투명이 앞, 블렌딩이 뒤 (블렌딩끼리의 순서는 그릴 때 거리로 다시 정함)
		if (a.pipeline.index != b.pipeline.index) return a.pipeline.index < b.pipeline.index;
		if (a.material != b.material) return a.material < b.material;
		if (a.mesh != b.mesh) return a.mesh < b.mesh;
		return a.indexStart < b.indexStart;
	});
}

void Renderer::UploadObjectConstants(uint32_t object, bool withHistory)
{
	// b1 PerObject. 드로우마다 Map(WRITE_DISCARD). 드라이버가 매번 새 메모리를 주므로
	// 앞 드로우가 아직 실행되지 않았어도 그 데이터를 덮어쓰지 않음. 바인딩은 프레임 초에 한 번만 했음.
	// 행렬은 BuildDrawList 가 계산해 둔 것 — 여기서는 복사만 함.
	const ObjectData& data = m_objects[object];
	PerObjectConstants perObject;
	perObject.world = data.world;
	perObject.worldInvTranspose = data.worldInvTranspose;
	perObject.prevWorld = withHistory ? data.prevWorld : data.world;
	perObject.highlightTint = data.highlightTint;     // 그림자 패스(알파 테스트 변형이 아니면 PS 없음)에서도 버퍼 전체를 채워 올림 — perObject 는 초기화하지 않은 지역 변수
	perObject.highlightFlash = data.highlightFlash;
	perObject.highlightRim = data.highlightRim;
	perObject.dissolveEdge = data.dissolveEdge;   // 그림자 알파 PS 도 읽음
	m_device->UpdateBuffer(m_perObjectCB, &perObject, sizeof(perObject));

	if (data.skinned)
	{
		// b7 Skin: 조인트 팔레트를 전치해서 올림 (world 와 같은 관례). 팔레트는 Animator 나 ApplyRestSkinPoses 가 미리 채워 둠.
		// 그래도 비었으면(모델 정보를 못 찾음) 단위행렬 — 메시 정점이 모델 공간 그대로 그려짐.
		// 팔레트가 있으면 그 길이만큼만 올림 (셰이더는 정점이 가리키는 조인트만 읽음). 없으면 정점이 어느 조인트를 가리킬지
		// 모르므로 전부 단위행렬로 올림 — 일부만 올리면 나머지는 정의되지 않은 값이 돼 정점이 엉뚱한 곳으로 감.
		const std::vector<XMFLOAT4X4>* palette = data.skinPalette;
		const uint32_t count = palette != nullptr ? (std::min)(static_cast<uint32_t>(palette->size()), kMaxSkinJoints) : 0;
		const uint32_t uploaded = count > 0 ? count : kMaxSkinJoints;
		for (uint32_t j = 0; j < uploaded; ++j)
		{
			if (j < count)
			{
				XMStoreFloat4x4(&m_skinScratch[j], XMMatrixTranspose(XMLoadFloat4x4(&(*palette)[j])));
			}
			else
			{
				XMStoreFloat4x4(&m_skinScratch[j], XMMatrixIdentity());
			}
		}
		m_device->UpdateBuffer(m_skinCB, m_skinScratch.data(), uploaded * static_cast<uint32_t>(sizeof(XMFLOAT4X4)));

		if (withHistory)
		{
			// b13 PrevSkin: 지난 프레임 팔레트. 없거나 조인트 수가 다르면 (첫 프레임·모델 교체) 방금 만든 이번 팔레트를 그대로 —
			// 스키닝 때문에 생기는 속도는 0 (World 변화로 생기는 속도는 그대로 남음).
			const MotionHistory& prev = m_motion[m_motionCurrent ^ 1u];
			if (data.prevPaletteCount > 0 && data.prevPaletteCount == count)
			{
				for (uint32_t j = 0; j < count; ++j)
				{
					XMStoreFloat4x4(&m_skinScratch[j], XMMatrixTranspose(XMLoadFloat4x4(&prev.palettes[data.prevPaletteOffset + j])));
				}
			}
			m_device->UpdateBuffer(m_prevSkinCB, m_skinScratch.data(), uploaded * static_cast<uint32_t>(sizeof(XMFLOAT4X4)));
		}
	}
}

void Renderer::RenderScenePass(RHI::CommandList& cmd, const Scene& scene, ViewTarget& view, const Camera& camera, const char* debugName, uint32_t viewIndex)
{
	// HDR: 씬은 선형 FP16 텍스처(view.hdr)에 그림. 최종 출력(씬 뷰 텍스처 또는 백버퍼)과 그 상태 전이는 톤
	// 패스가 맡음. 깊이는 뷰가 가진 텍스처 — 에디터든 게임 모드든 같은 경로임. 파티클·디버그 선이 없으면
	// 깊이는 DepthWrite 로 끝나고, 있으면 두 번째 패스 때문에 DepthRead 로 끝남 (다음 프레임 이 함수 처음의 배리어가 되돌림).
	if (UseDeferred(view))
	{
		// 디퍼드 (2단계): G-buffer → (SSAO) → 풀스크린 라이팅 → 포워드 (G-buffer 에 못 담는 불투명·하늘·블렌딩).
		// 끝나면 hdr 는 RenderTarget, depth 는 DepthWrite — 아래 파티클 패스가 이어받는 상태가 포워드 경로와 같음.
		RenderDeferredScene(cmd, scene, view, camera, debugName, viewIndex);
	}
	else
	{
		cmd.Barrier(view.hdr, ResourceState::RenderTarget);
		cmd.Barrier(view.velocity, ResourceState::RenderTarget);
		cmd.Barrier(view.depth, ResourceState::DepthWrite);

		const RenderPassDesc pass{
			.colors = { {
							.texture = view.hdr,
							.load = LoadOp::Clear,
							.srgbView = false,                                                                                      // FP16 에는 sRGB 뷰가 없음. 선형 값을 그대로 씀 — sRGB 인코딩은 톤 패스의 RTV 가 함
							.clearColor = { scene.clearColor[0], scene.clearColor[1], scene.clearColor[2], scene.clearColor[3] },   // 선형. 노출·ACES 를 거침
						},
						// 속도 (0 = 움직임 없음으로 지움). 하늘이 없는 씬의 배경도 0
						{ .texture = view.velocity, .load = LoadOp::Clear, .srgbView = false, .clearColor = { 0.0f, 0.0f, 0.0f, 0.0f } } },
			.colorCount = 2,
			.depth = { .texture = view.depth, .load = LoadOp::Clear },
			.debugName = debugName,
		};
		cmd.BeginRenderPass(pass);
		DrawItems(cmd, scene, camera, ObjectCullCamera(camera, viewIndex), DrawMode::All);
		cmd.EndRenderPass();
	}

	// 두 번째 패스: 파티클과 디버그 선. 깊이를 "읽기만" 함 — 읽기 전용 DSV 로 깊이 테스트를 하면서 같은 깊이를
	// 셰이더가 SRV 로 읽을 수 있음 (소프트 파티클·깊이 충돌). 그릴 것이 없으면 패스를 열지 않음 (이전과 같은 패스 수).
	const bool drawParticles = !m_activeEmitterKeys.empty();
	const bool drawRain = m_frameRain.drawRain;
	if (drawParticles || drawRain || m_debugLineCount > 0)
	{
		cmd.Barrier(view.depth, ResourceState::DepthRead);
		if (viewIndex == 0 && drawParticles)
		{
			// 파티클 시뮬레이션 (컴퓨트). 렌더 패스 밖이어야 하고, 미리보기 패스가 같은 결과를 그리므로 프레임에 한 번
			// (메인 뷰). 이번 프레임 불투명 깊이가 다 그려진 뒤라 충돌이 이번 프레임 깊이를 씀. 끝나면 파티클 버퍼·생존
			// 목록은 ShaderResource (VS t21, t22), 인자 버퍼는 IndirectArgument.
			GpuProfileScope gpuScope(cmd, "ParticleSim");
			SimulateParticles(cmd, view);
		}
		if (drawParticles)
		{
			GpuProfileScope gpuScope(cmd, "ParticleSort");
			SortParticles(cmd, viewIndex);   // 알파 이미터를 이 뷰의 카메라로 먼 것부터 (없으면 아무것도 안 함)
		}
		std::string effectsName = std::string(debugName) + "Effects";
		const RenderPassDesc effects{
			.colors = { { .texture = view.hdr, .load = LoadOp::Load } },
			.colorCount = 1,
			.depth = { .texture = view.depth, .load = LoadOp::Load, .readOnly = true },
			.debugName = effectsName.c_str(),
		};
		cmd.BeginRenderPass(effects);
		cmd.SetResourceSet(kSetFrame, m_frameSet);   // 컴퓨트를 지났고 새 패스이므로 다시 꽂음 (b0 = 이 뷰의 카메라)
		cmd.SetResourceSet(kSetObject, m_objectSet);
		if (drawParticles)
		{
			// 셋 1 을 파티클 셋으로 바꾸므로 끝나면 오브젝트 셋을 되돌림 (다음 패스가 셋 1 = 오브젝트 셋을 전제할 수 있음).
			DrawParticles(cmd, view, viewIndex);
			cmd.SetResourceSet(kSetObject, m_objectSet);
		}
		if (drawRain)
		{
			// 빗줄기·튐 (RendererRain.cpp). 셋 1 을 비 셋으로 바꾸므로 끝나면 오브젝트 셋을 되돌림.
			GpuProfileScope gpuScope(cmd, "Rain");
			DrawRain(cmd, view);
			cmd.SetResourceSet(kSetObject, m_objectSet);
		}
		DrawDebugLines(cmd);   // 깊이 테스트가 없으므로 항상 위에 그려짐
		cmd.EndRenderPass();
	}

	// 톤 패스가 t13 으로 읽음. 속도는 후처리(속도 디버그·모션 블러·TAA)가 t7 로 읽음.
	cmd.Barrier(view.hdr, ResourceState::ShaderResource);
	cmd.Barrier(view.velocity, ResourceState::ShaderResource);
}

void Renderer::DrawItems(RHI::CommandList& cmd, const Scene& scene, const Camera& camera, const Camera& cullCamera, DrawMode mode)
{
	// 그림자 패스에서 t8 바인딩이 해제됐고, 후처리 패스가 셋 인덱스 0 에 후처리 셋을 꽂았을 수 있으므로
	// 프레임 셋을 다시 바인딩함 (이제 그림자 맵은 SRV 상태). 패스마다 새로 바인딩함.
	cmd.SetResourceSet(kSetFrame, m_frameSet);
	cmd.SetResourceSet(kSetObject, m_objectSet);

	// 컬링 카메라의 절두체. 경계 구가 완전히 밖에 있는 오브젝트는 드로우(상수 업로드 포함)를 건너뜀. 보통 그리는 카메라와
	// 같고, 컬링 고정 중 메인 뷰만 고정된 사본 (ObjectCullCamera). 반투명 정렬의 눈 위치는 그리는 카메라.
	const Frustum frustum = Frustum::FromViewProj(cullCamera.GetViewMatrix() * cullCamera.GetProjectionMatrix());

	// ---- 드로우. "직전" 상태 기억은 이 패스 안에서만 유효함 ----
	PipelineHandle lastPipeline;
	const GpuMaterial* lastMaterial = nullptr;
	const GpuMesh* lastMesh = nullptr;

	const bool gbufferPass = mode == DrawMode::GBuffer;
	const auto drawItem = [&](const DrawItem& item)
	{
		const PipelineHandle pipeline = gbufferPass ? item.gbufferPipeline : item.pipeline;
		if (pipeline != lastPipeline)
		{
			cmd.SetPipelineState(pipeline);
			lastPipeline = pipeline;
			++m_stats.pipelineSwitches;
		}

		if (item.gpuMaterial != lastMaterial)
		{
			cmd.SetResourceSet(kSetMaterial, item.gpuMaterial->set);
			lastMaterial = item.gpuMaterial;
			++m_stats.resourceSetSwitches;
		}

		if (item.gpuMesh != lastMesh)
		{
			cmd.SetVertexBuffer(item.gpuMesh->vertexBuffer);
			cmd.SetIndexBuffer(item.gpuMesh->indexBuffer, item.gpuMesh->indexFormat);
			lastMesh = item.gpuMesh;
			++m_stats.meshSwitches;
		}

		// 서브메시 범위만 그림. 같은 메시의 다른 서브메시는 정렬 덕에 대개 바로 뒤에 옴 (VB/IB 재바인딩 없음).
		UploadObjectConstants(item.object, true);
		cmd.DrawIndexed(item.indexCount, item.indexStart, 0);
		m_stats.triangles += item.indexCount / 3;
		++m_stats.draws;
	};

	m_blendedScratch.clear();
	for (const DrawItem& item : m_drawList)
	{
		// 이 패스가 맡은 항목만 (컬링 통계도 맡은 항목만 세어 디퍼드 프레임에서 두 번 세지 않음).
		// G-buffer 패스 = deferred 불투명, 디퍼드의 포워드 패스 = 나머지 (deferred 아닌 불투명 + 블렌딩), All = 전부.
		const bool mine = mode == DrawMode::All || (gbufferPass ? (item.deferred && !item.blended) : (!item.deferred || item.blended));
		if (!mine) continue;
		if (!frustum.Intersects(m_objects[item.object].sphere))
		{
			++m_stats.culled;
			continue;
		}
		if (item.blended)
		{
			m_blendedScratch.push_back(&item);   // 하늘 뒤에서 따로 그림
		}
		else
		{
			drawItem(item);
			if (gbufferPass)
			{
				++m_stats.gbufferDraws;
			}
			else if (mode == DrawMode::ForwardRest)
			{
				++m_stats.forwardOpaqueDraws;
			}
		}
	}
	if (gbufferPass) return;   // G-buffer 패스에는 하늘·블렌딩이 없음 (블렌딩 항목은 위에서 걸러져 목록이 비어 있음)

	// 불투명 물체를 다 그린 뒤 하늘. 깊이 버퍼가 채워져 있으므로 가려진 픽셀은 깊이 테스트에서 떨어짐.
	if (!scene.environment.skybox.empty())
	{
		DrawSkybox(cmd);
	}

	// 알파 블렌드·투과 재질: 하늘까지 그린 뒤, 카메라에서 먼 것부터(오브젝트 경계 구 중심 기준). 깊이는 테스트만 하고 쓰지 않음.
	// 하늘 패스가 PSO 를 바꿨으므로 직전 상태 기억을 비우고 프레임·오브젝트 셋을 다시 꽂음.
	if (!m_blendedScratch.empty())
	{
		const XMFLOAT3 eye = camera.GetPosition();
		const auto distanceSq = [&](const DrawItem* item)
		{
			const XMFLOAT4& s = m_objects[item->object].sphere;
			const float dx = s.x - eye.x, dy = s.y - eye.y, dz = s.z - eye.z;
			return dx * dx + dy * dy + dz * dz;
		};
		std::stable_sort(m_blendedScratch.begin(), m_blendedScratch.end(), [&](const DrawItem* a, const DrawItem* b)
		{
			return distanceSq(a) > distanceSq(b);
		});
		cmd.SetResourceSet(kSetFrame, m_frameSet);
		cmd.SetResourceSet(kSetObject, m_objectSet);
		lastPipeline = PipelineHandle{};
		lastMaterial = nullptr;
		lastMesh = nullptr;
		for (const DrawItem* item : m_blendedScratch)
		{
			drawItem(*item);
		}
	}

	// 파티클·디버그 선은 여기가 아니라 RenderScenePass 의 두 번째 패스 (깊이를 읽기 전용으로 바꾼 뒤) 에서 그림.
}

void Renderer::UploadDebugLines(const DebugDraw& debugDraw)
{
	// D3D12 의 Dynamic 버퍼는 프레임마다 한 번은 써야 바인딩됨. 선이 없어도 정점 하나 분량을 올리고 그리지는 않음 (m_debugLineCount = 0).
	const std::vector<DebugLineVertex>& vertices = debugDraw.GetVertices();

	m_debugLineCount = static_cast<uint32_t>(vertices.size());
	static const DebugLineVertex kNoLine = {};
	m_device->UpdateBuffer(m_debugLineVB, m_debugLineCount > 0 ? vertices.data() : &kNoLine, (std::max)(m_debugLineCount, 1u) * static_cast<uint32_t>(sizeof(DebugLineVertex)));
}

void Renderer::DrawDebugLines(RHI::CommandList& cmd)
{
	if (m_debugLineCount == 0) return;
	const PipelineHandle pipeline = GetDebugLinePipeline();
	if (!pipeline.IsValid()) return;
	cmd.SetPipelineState(pipeline);
	cmd.SetVertexBuffer(m_debugLineVB);
	cmd.Draw(m_debugLineCount, 0);
	++m_stats.pipelineSwitches;
	++m_stats.draws;
}

PipelineHandle Renderer::GetDebugLinePipeline()
{
	if (m_debugLinePipeline.IsValid()) return m_debugLinePipeline;
	// 메인 패스와 같은 타깃(FP16 HDR + D24S8)에 그림 — D3D12 PSO 는 포맷이 같아야 함. 깊이 테스트는 꺼 둠.
	PipelineStateDesc desc = m_passDesc[Pass_DebugLine];
	desc.rtvFormats[0] = Format::R16G16B16A16_FLOAT;
	desc.rtvCount = 1;
	desc.dsvFormat = Format::D24_UNORM_S8_UINT;
	m_debugLinePipeline = m_device->CreatePipeline(desc);
	return m_debugLinePipeline;
}

void Renderer::DrawSkybox(RHI::CommandList& cmd)
{
	const PipelineHandle pipeline = GetSkyPipeline();
	if (!pipeline.IsValid()) return;
	cmd.SetPipelineState(pipeline);
	++m_stats.pipelineSwitches;
	// 프레임 셋(b0 view/proj, t9 큐브, s5)은 DrawItems 앞에서 이미 바인딩됨. 정점 버퍼는 쓰지 않음 — VS 가
	// SV_VertexID 0, 1, 2 로 삼각형을 만듦.
	cmd.Draw(3, 0);
	++m_stats.draws;
	m_stats.triangles += 1;
}

void Renderer::UploadPerFrameConstants(const Camera& camera, ViewTarget& target, float totalTime, bool ssaoReady)
{
	const uint32_t width = target.width;
	const uint32_t height = target.height;
	// HLSL은 column_major로 읽고 셰이더는 mul(v, M)을 쓰므로 전치해서 올림 (ShaderConstants.h).
	// 미리보기 패스에서 다른 카메라로 한 번 더 호출함 (업로드 링을 쓰므로 프레임 안에서 여러 번 올려도 됨).
	const XMMATRIX view = camera.GetViewMatrix();
	const XMMATRIX unjitteredProj = camera.GetProjectionMatrix();
	// TAA 지터: 투영 결과를 NDC 에서 (jx, jy) 만큼 옮기는 행렬 J 를 곱함 (원근이면 clip.xy += j · w, 직교면 w = 1 이라 그대로 더함).
	// 위상 k = 이 뷰의 프레임 수 % 8, 오프셋 = Halton(k+1, 2·3) − 0.5 (픽셀). 같은 프레임 번호면 실행마다 같은 지터 (스크린샷 결정적).
	// b0 의 proj·viewProj·invViewProj 에만 들어가고 카메라 행렬(컬링·그림자)과 unjitteredViewProj(속도)는 그대로.
	XMFLOAT2 jitter(0.0f, 0.0f);
	if (TaaActive() && m_settings.taaJitter && width > 0 && height > 0)
	{
		const auto halton = [](uint32_t index, uint32_t base)
		{
			float f = 1.0f, r = 0.0f;
			for (uint32_t i = index; i > 0; i /= base)
			{
				f /= static_cast<float>(base);
				r += f * static_cast<float>(i % base);
			}
			return r;
		};
		const uint32_t phase = target.temporal.frameIndex % 8 + 1;
		jitter.x = 2.0f * (halton(phase, 2) - 0.5f) / static_cast<float>(width);
		jitter.y = -2.0f * (halton(phase, 3) - 0.5f) / static_cast<float>(height);
	}
	const XMMATRIX proj = unjitteredProj * XMMatrixTranslation(jitter.x, jitter.y, 0.0f);   // 행 벡터 관례: clip' = clip · P · J
	PerFrameConstants perFrame = {};
	XMStoreFloat4x4(&perFrame.view, XMMatrixTranspose(view));
	XMStoreFloat4x4(&perFrame.proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&perFrame.viewProj, XMMatrixTranspose(view * proj));
	perFrame.cameraPosition = camera.GetPosition();
	perFrame.time = totalTime;
	perFrame.shadowParams = XMFLOAT4(1.0f / kShadowAtlasSize, m_shadowSettings.bias, m_shadowSettings.strength, m_shadowEnabledThisFrame ? 1.0f : 0.0f);
	perFrame.debugParams = XMFLOAT4(static_cast<float>(m_settings.debugView), m_settings.debugDepthRange, 0.0f, 0.0f);
	// ResolveEnvironment 가 이번 프레임에 정한 환경 (Render 맨 앞에서 불림)
	perFrame.environmentParams = XMFLOAT4(m_environment.iblIntensity, static_cast<float>(m_environment.specularMips), m_environment.irradianceLod, m_environment.hasEnvironment ? 1.0f : 0.0f);
	perFrame.outputParams = XMFLOAT4(m_environment.exposure, m_environment.toneMapping ? 1.0f : 0.0f, m_environment.hasBrdfLut ? 1.0f : 0.0f, 0.0f);
	perFrame.shadowExtra = XMFLOAT4(m_shadowSettings.normalOffset, 0.0f, 0.0f, 0.0f);
	perFrame.iblTint = XMFLOAT4(m_environment.iblTint.x, m_environment.iblTint.y, m_environment.iblTint.z, 0.0f);
	// 디퍼드: 깊이 → 월드 위치 복원용 viewProj⁻¹, 픽셀 → UV 용 타깃 크기.
	XMStoreFloat4x4(&perFrame.invViewProj, XMMatrixTranspose(XMMatrixInverse(nullptr, view * proj)));
	const float w = static_cast<float>((std::max)(width, 1u));
	const float h = static_cast<float>((std::max)(height, 1u));
	perFrame.screenParams = XMFLOAT4(w, h, 1.0f / w, 1.0f / h);
	// 클러스터드: 켜짐·원근 여부·near·far 와 깊이 → 슬라이스 식의 계수 (C++ 비닝과 같은 식, ClusterDepthParams).
	perFrame.clusterParams = XMFLOAT4(m_settings.clustered ? 1.0f : 0.0f, camera.IsUsePerspectiveProjection() ? 1.0f : 0.0f, camera.GetNearZ(), camera.GetFarZ());
	perFrame.clusterDepth = ClusterDepthParams(camera);
	// SSAO: 이 뷰에 결과가 있으면 라이팅이 t29 를 읽음. 합치는 방식은 씬 설정.
	perFrame.aoParams = XMFLOAT4(ssaoReady ? 1.0f : 0.0f, m_ssaoSettings.multiply ? 1.0f : 0.0f, 0.0f, 0.0f);
	// 비 (PrepareRain 이 이번 프레임에 정함. 비가 없으면 0 — 젖음·디버그 뷰 18 이 아무것도 하지 않음). 두 뷰가 같은 값.
	perFrame.rainParams = m_frameRain.rainParams;
	perFrame.rainMapXZ = m_frameRain.rainMapXZ;
	perFrame.rainMapY = m_frameRain.rainMapY;
	perFrame.rainSurface = m_frameRain.rainSurface;

	// ---- 속도 버퍼·TAA: 지터 없는 이번·지난 viewProj. 이 뷰를 그린 카메라가 바뀌었으면 (미리보기 대상 변경·재생 전환)
	// 지난 프레임이 없는 것으로 봄 → 이전 = 이번 (속도 0) ----
	TemporalState& temporal = target.temporal;
	if (temporal.lastCamera != &camera)
	{
		temporal.hasPrev = false;
		temporal.historyValid = false;
		temporal.lastCamera = &camera;
	}
	XMStoreFloat4x4(&perFrame.unjitteredViewProj, XMMatrixTranspose(view * unjitteredProj));
	perFrame.prevUnjitteredViewProj = temporal.hasPrev ? temporal.prevUnjitteredViewProj : perFrame.unjitteredViewProj;
	perFrame.temporalParams = XMFLOAT4(jitter.x, jitter.y, temporal.historyValid ? 1.0f : 0.0f, static_cast<float>(temporal.frameIndex % 8));
	temporal.prevUnjitteredViewProj = perFrame.unjitteredViewProj;
	temporal.hasPrev = true;
	++temporal.frameIndex;
	m_device->UpdateBuffer(m_perFrameCB, &perFrame, sizeof(perFrame));
}

// ------------------------------------------------------------------ GPU 캐시

const Renderer::GpuMesh* Renderer::GetOrCreateGpuMesh(const Mesh& mesh)
{
	auto found = m_gpuMeshes.find(&mesh);
	if (found != m_gpuMeshes.end())
	{
		return &found->second;
	}
	if (!mesh.IsValid())
	{
		return nullptr;
	}

	// 정점/인덱스 버퍼는 바뀌지 않으므로 Default 버퍼에 초기 데이터를 넣어 만듦.
	GpuMesh gpuMesh;
	gpuMesh.vertexFormat = mesh.GetVertexFormat();

	const uint32_t vertexStride = GetVertexStride(mesh.GetVertexFormat());
	const BufferDesc vbDesc{
		.size = static_cast<uint32_t>(vertexStride * mesh.GetVertices().size()),
		.usage = BufferUsage::Default,
		.bindFlags = BufferBind_Vertex,
		.stride = vertexStride,
		.debugName = "VB",
	};
	if (mesh.IsSkinned())
	{
		// 스키닝 포맷: 정점마다 VERTEX 뒤에 VertexSkin 을 이어 붙인 배열을 올림 (VERTEX_SKINNED)
		std::vector<VERTEX_SKINNED> skinnedVertices(mesh.GetVertices().size());
		for (size_t i = 0; i < skinnedVertices.size(); ++i)
		{
			skinnedVertices[i].base = mesh.GetVertices()[i];
			skinnedVertices[i].skin = mesh.GetSkinVertices()[i];
		}
		gpuMesh.vertexBuffer = m_device->CreateBuffer(vbDesc, skinnedVertices.data());
	}
	else
	{
		gpuMesh.vertexBuffer = m_device->CreateBuffer(vbDesc, mesh.GetVertices().data());
	}

	// 정점이 65,536개 미만이면 16비트 인덱스. 버퍼 크기와 IA 대역폭이 절반으로 줄어듦. CPU 쪽은 32비트 그대로 둠.
	// size / stride 는 아래 분기에서 채우므로 여기서는 공통 필드만 지정함.
	BufferDesc ibDesc{ .usage = BufferUsage::Default, .bindFlags = BufferBind_Index, .debugName = "IB" };
	if (mesh.GetVertexCount() <= 65535)
	{
		std::vector<uint16_t> indices16(mesh.GetIndices().size());
		for (size_t i = 0; i < indices16.size(); ++i)
		{
			indices16[i] = static_cast<uint16_t>(mesh.GetIndices()[i]);
		}
		ibDesc.size = static_cast<uint32_t>(sizeof(uint16_t) * indices16.size());
		ibDesc.stride = sizeof(uint16_t);
		gpuMesh.indexFormat = Format::R16_UINT;
		gpuMesh.indexBuffer = m_device->CreateBuffer(ibDesc, indices16.data());
	}
	else
	{
		ibDesc.size = static_cast<uint32_t>(sizeof(uint32_t) * mesh.GetIndices().size());
		ibDesc.stride = sizeof(uint32_t);
		gpuMesh.indexFormat = Format::R32_UINT;
		gpuMesh.indexBuffer = m_device->CreateBuffer(ibDesc, mesh.GetIndices().data());
	}

	gpuMesh.indexCount = mesh.GetIndexCount();

	if (!gpuMesh.vertexBuffer.IsValid() || !gpuMesh.indexBuffer.IsValid())
	{
		Log::Error("GpuMesh 생성 실패 (정점 %zu, 인덱스 %u).", mesh.GetVertices().size(), mesh.GetIndexCount());
		m_device->DestroyBuffer(gpuMesh.vertexBuffer);
		m_device->DestroyBuffer(gpuMesh.indexBuffer);
		return nullptr;
	}

	return &m_gpuMeshes.emplace(&mesh, gpuMesh).first->second;
}

const Renderer::GpuMaterial* Renderer::GetOrCreateGpuMaterial(const Material& material, const Scene* scene)
{
	GpuMaterial* gpuMaterial = nullptr;
	auto found = m_gpuMaterials.find(&material);
	if (found != m_gpuMaterials.end())
	{
		gpuMaterial = &found->second;
	}
	else
	{
		GpuMaterial created;

		const BufferDesc cbDesc{ .size = sizeof(MaterialConstants), .usage = BufferUsage::Dynamic, .bindFlags = BufferBind_Constant, .debugName = "CB_Material" };
		created.constants = m_device->CreateBuffer(cbDesc);
		if (!created.constants.IsValid())
		{
			Log::Error("GpuMaterial 상수버퍼 생성 실패 (%s).", material.name.c_str());
			return nullptr;
		}
		gpuMaterial = &m_gpuMaterials.emplace(&material, created).first->second;
	}

	// 셋에 들어가는 입력(텍스처 이름 열하나·알베도 색공간·샘플러)이 셋을 만들 때와 같고 텍스처 캐시 세대
	// (GetGeneration — ReleaseLoaded·Evict 가 올림)가 그 뒤로 그대로면, 이름 → 핸들 조회를 건너뜀. 대부분의 프레임이 이 경로임 — 재질 편집(ImGui), 샘플러
	// 덮어쓰기, 텍스처 다시 읽기(Evict), 씬 교체 때만 아래로 내려감.
	const SamplerPreset preset = (m_settings.samplerOverride >= 0 && m_settings.samplerOverride < static_cast<int>(kSamplerPresetCount))
		? static_cast<SamplerPreset>(m_settings.samplerOverride)
		: material.sampler;
	const SamplerHandle sampler = GetSampler(preset);
	const bool unchanged = gpuMaterial->set.IsValid() && gpuMaterial->textureGeneration == m_textures.GetGeneration() && gpuMaterial->sampler == sampler && gpuMaterial->albedoSrgb == material.albedoSrgb && gpuMaterial->albedoName == material.albedoTexture && gpuMaterial->normalName == material.normalTexture && gpuMaterial->metallicRoughnessName == material.metallicRoughnessTexture && gpuMaterial->occlusionName == material.occlusionTexture && gpuMaterial->emissiveName == material.emissiveTexture && gpuMaterial->specularName == material.specularTexture && gpuMaterial->specularColorName == material.specularColorTexture && gpuMaterial->clearcoatName == material.clearcoatTexture && gpuMaterial->clearcoatRoughnessName == material.clearcoatRoughnessTexture && gpuMaterial->clearcoatNormalName == material.clearcoatNormalTexture && gpuMaterial->transmissionName == material.transmissionTexture && gpuMaterial->dissolveNoiseName == material.dissolveNoiseTexture;
	if (!unchanged)
	{
		const TextureHandle albedo = m_textures.Load2D(material.albedoTexture, material.albedoSrgb, scene);
		const TextureHandle normal = material.normalTexture.empty() ? m_textures.GetFlatNormal() : m_textures.Load2D(material.normalTexture, false, scene);
		// PBR: 빈 이름은 흰색 — 금속성/거칠기·AO·발광 모두 계수가 그대로 쓰임. 발광만 색이라 sRGB.
		const TextureHandle metallicRoughness = m_textures.Load2D(material.metallicRoughnessTexture, false, scene);
		const TextureHandle occlusion = m_textures.Load2D(material.occlusionTexture, false, scene);
		const TextureHandle emissive = m_textures.Load2D(material.emissiveTexture, true, scene);
		// 반사 확장: 빈 이름은 흰색 (factor 가 그대로 쓰임). specularTexture 는 A 채널을 읽는 선형 데이터, 반사 색은 sRGB.
		const TextureHandle specular = m_textures.Load2D(material.specularTexture, false, scene);
		const TextureHandle specularColor = m_textures.Load2D(material.specularColorTexture, true, scene);
		// 클리어코트: 세기·거칠기는 흰색 대체, 코팅 노멀은 평평한 노멀 대체 (기하 노멀과 같아짐). 모두 선형.
		const TextureHandle clearcoat = m_textures.Load2D(material.clearcoatTexture, false, scene);
		const TextureHandle clearcoatRoughness = m_textures.Load2D(material.clearcoatRoughnessTexture, false, scene);
		const TextureHandle transmission = m_textures.Load2D(material.transmissionTexture, false, scene);   // 흰색 대체: 계수가 그대로 쓰임
		const TextureHandle clearcoatNormal = material.clearcoatNormalTexture.empty() ? m_textures.GetFlatNormal() : m_textures.Load2D(material.clearcoatNormalTexture, false, scene);
		// 디졸브 노이즈: 빈 이름은 흰색이 아니라 엔진 기본 노이즈 — 흰색(1)이면 진행도가 끝날 때까지 안 사라지다 한꺼번에 사라짐.
		const TextureHandle dissolveNoise = m_textures.Load2D(material.dissolveNoiseTexture.empty() ? std::string("builtin:noise") : material.dissolveNoiseTexture, false, scene);

		// 실제로 꽂히는 핸들이 달라졌을 때만 ResourceSet 을 다시 만듦. ResourceSet은 값 객체라 부분 갱신 기능이
		// 없음 — D3D12 디스크립터 테이블도 다시 써야 함.
		if (!gpuMaterial->set.IsValid() || gpuMaterial->boundAlbedo != albedo || gpuMaterial->boundNormal != normal || gpuMaterial->boundSampler != sampler || gpuMaterial->boundMetallicRoughness != metallicRoughness || gpuMaterial->boundOcclusion != occlusion || gpuMaterial->boundEmissive != emissive || gpuMaterial->boundSpecular != specular || gpuMaterial->boundSpecularColor != specularColor || gpuMaterial->boundClearcoat != clearcoat || gpuMaterial->boundClearcoatRoughness != clearcoatRoughness || gpuMaterial->boundClearcoatNormal != clearcoatNormal || gpuMaterial->boundTransmission != transmission || gpuMaterial->boundDissolveNoise != dissolveNoise)
		{
			m_device->DestroyResourceSet(gpuMaterial->set);
			// 슬롯 순서는 CreateLayouts 의 materialLayout.Add(...) 순서: b3 상수, t0 알베도, t1 노멀, t2
			// 금속성/거칠기, t3 AO, t4 발광, t5 반사 세기, t6 반사 색, t16~t18 클리어코트, t19 투과, t20 디졸브 노이즈, s0 샘플러.
			const ResourceSetDesc setDesc{
				.layout = m_layouts[Layout_Material],
				.bindings = {
					{ .buffer = gpuMaterial->constants },
					{ .texture = albedo },
					{ .texture = normal },
					{ .texture = metallicRoughness },
					{ .texture = occlusion },
					{ .texture = emissive },
					{ .texture = specular },
					{ .texture = specularColor },
					{ .texture = clearcoat },
					{ .texture = clearcoatRoughness },
					{ .texture = clearcoatNormal },
					{ .texture = transmission },
					{ .texture = dissolveNoise },
					{ .sampler = sampler },
				},
				.debugName = "Set_Material",
			};
			gpuMaterial->set = m_device->CreateResourceSet(setDesc);
			gpuMaterial->boundAlbedo = albedo;
			gpuMaterial->boundNormal = normal;
			gpuMaterial->boundMetallicRoughness = metallicRoughness;
			gpuMaterial->boundOcclusion = occlusion;
			gpuMaterial->boundEmissive = emissive;
			gpuMaterial->boundSpecular = specular;
			gpuMaterial->boundSpecularColor = specularColor;
			gpuMaterial->boundClearcoat = clearcoat;
			gpuMaterial->boundClearcoatRoughness = clearcoatRoughness;
			gpuMaterial->boundClearcoatNormal = clearcoatNormal;
			gpuMaterial->boundTransmission = transmission;
			gpuMaterial->boundDissolveNoise = dissolveNoise;
			gpuMaterial->boundSampler = sampler;
			if (!gpuMaterial->set.IsValid())
			{
				Log::Error("GpuMaterial ResourceSet 생성 실패 (%s).", material.name.c_str());
				return nullptr;
			}
		}
		gpuMaterial->albedoName = material.albedoTexture;
		gpuMaterial->normalName = material.normalTexture;
		gpuMaterial->metallicRoughnessName = material.metallicRoughnessTexture;
		gpuMaterial->occlusionName = material.occlusionTexture;
		gpuMaterial->emissiveName = material.emissiveTexture;
		gpuMaterial->specularName = material.specularTexture;
		gpuMaterial->specularColorName = material.specularColorTexture;
		gpuMaterial->clearcoatName = material.clearcoatTexture;
		gpuMaterial->clearcoatRoughnessName = material.clearcoatRoughnessTexture;
		gpuMaterial->clearcoatNormalName = material.clearcoatNormalTexture;
		gpuMaterial->transmissionName = material.transmissionTexture;
		gpuMaterial->dissolveNoiseName = material.dissolveNoiseTexture;
		gpuMaterial->albedoSrgb = material.albedoSrgb;
		gpuMaterial->sampler = sampler;
		gpuMaterial->textureGeneration = m_textures.GetGeneration();
	}

	// 재질 상수는 프레임에 한 번 올림. 편집(ImGui)이 바로 반영되도록 매 프레임 올리되,
	// 같은 재질을 여러 오브젝트가 쓰면 그중 첫 드로우에서만 올림. (D3D12 의 Dynamic 상수버퍼는 프레임마다
	// 갱신해야 하므로 "값이 같으면 건너뛰기"는 하지 않음.)
	if (gpuMaterial->uploadedFrame != m_frameNumber)
	{
		MaterialConstants constants = {};
		constants.baseColor = material.baseColor;
		constants.emissive = material.emissive;
		constants.metallic = material.metallic;
		constants.roughness = material.roughness;
		constants.occlusionStrength = material.occlusionStrength;
		constants.ior = material.ior;
		constants.specularColor = material.specularColor;
		constants.specularFactor = material.specularFactor;
		constants.clearcoatFactor = material.clearcoatFactor;
		constants.clearcoatRoughness = material.clearcoatRoughness;
		constants.clearcoatNormalScale = material.clearcoatNormalScale;
		constants.transmissionFactor = material.transmissionFactor;
		constants.blended = IsBlended(material) ? 1.0f : 0.0f;
		constants.uvScale = material.uvScale;
		constants.unlit = material.unlit ? 1.0f : 0.0f;
		constants.normalStrength = m_settings.normalMapping ? material.normalStrength : 0.0f;
		constants.alphaCutoff = material.alphaCutoff;
		constants.dissolveNoiseScale = material.dissolveNoiseScale;
		m_device->UpdateBuffer(gpuMaterial->constants, &constants, sizeof(constants));
		gpuMaterial->uploadedFrame = m_frameNumber;
	}
	return gpuMaterial;
}

// ------------------------------------------------------------------ PSO

PipelineHandle Renderer::GetPipelineFor(const Material& material, VertexFormat vertexFormat, bool mirrored, bool ocean)
{
	// 이 PSO 를 가르는 것은 정점 포맷·미러·채우기·컬링·블렌딩·바다 VS 여섯 가지뿐. 그 비트를 키로 핸들을 기억해 두면, 드로우마다
	// PipelineStateDesc 전체를 복사하고 해시해 Device 캐시를 찾는 일이 없어짐.
	const bool wireframe = m_settings.wireframe || material.wireframe;
	const bool cullBack = m_settings.cullBack && !material.doubleSided;
	const bool blended = IsBlended(material);
	const bool skinned = vertexFormat == VertexFormat::PositionColorNormalTexcoordTangentSkin;
	const bool oceanVs = ocean && !skinned;   // 바다 VS 는 스킨 정점을 읽지 않음
	const uint32_t key = static_cast<uint32_t>(vertexFormat) | (mirrored ? 1u << 8 : 0u) | (wireframe ? 1u << 9 : 0u) | (cullBack ? 1u << 10 : 0u) | (blended ? 1u << 11 : 0u) | (oceanVs ? 1u << 12 : 0u);
	auto found = m_mainPipelines.find(key);
	if (found != m_mainPipelines.end()) return found->second;

	PipelineStateDesc desc = m_passDesc[skinned ? Pass_MainSkin : (oceanVs ? Pass_Ocean : Pass_Main)];
	desc.vertexLayout = GetVertexLayout(vertexFormat);
	desc.rasterizer.frontCounterClockwise = mirrored ? !desc.rasterizer.frontCounterClockwise : desc.rasterizer.frontCounterClockwise;
	desc.rasterizer.fill = wireframe ? FillMode::Wireframe : FillMode::Solid;
	desc.rasterizer.cull = cullBack ? CullMode::Back : CullMode::None;
	if (blended)
	{
		// 알파 블렌드·투과: 셰이더가 rgb 에 알파를 미리 곱해 내보냄 → 최종 = Src + Dst · (1 − SrcAlpha). 깊이는 테스트만 함
		// (뒤의 불투명 물체에 가려지되, 반투명끼리는 먼 것부터 그려 서로 쓰지 않음).
		desc.blend.rt[0].blendEnable = true;
		desc.blend.rt[0].srcColor = BlendFactor::One;
		desc.blend.rt[0].dstColor = BlendFactor::InvSrcAlpha;
		desc.blend.rt[0].srcAlpha = BlendFactor::One;
		desc.blend.rt[0].dstAlpha = BlendFactor::InvSrcAlpha;
		desc.depthStencil.depthWrite = false;
		// 속도 RT(1)는 쓰지 않음 — 반투명은 속도를 갖지 않고 뒤 불투명 표면의 속도가 남음. RT 별 설정이라 independentBlend.
		desc.blend.independentBlend = true;
		desc.blend.rt[1] = RenderTargetBlendDesc{ .writeMask = 0 };
	}
	// D3D12 PSO 는 렌더 타깃 뷰의 포맷을 알아야 함. 메인 패스는 FP16 HDR 텍스처 + 속도(RG16F)에 그림.
	// (sRGB 여부는 톤 패스의 PSO 만 따짐 — GetTonemapPipeline)
	desc.rtvFormats[0] = Format::R16G16B16A16_FLOAT;
	desc.rtvFormats[1] = kVelocityFormat;
	desc.rtvCount = 2;
	desc.dsvFormat = Format::D24_UNORM_S8_UINT;
	const PipelineHandle handle = m_device->CreatePipeline(desc);
	if (handle.IsValid())
	{
		m_mainPipelines.emplace(key, handle);   // 실패는 기억하지 않음 (다음에 다시 시도)
	}
	return handle;
}

PipelineHandle Renderer::GetShadowPipelineFor(const Material& material, VertexFormat vertexFormat, bool mirrored, bool alphaTest, bool noBias)
{
	// 그림자는 항상 솔리드로 그림 (와이어프레임 재질도 실체는 있음). 양면 재질은 컬링 없이 그림.
	// 알파 테스트(컷아웃·디졸브)는 PS 가 있는 변형 — 셋도 재질 셋까지 셋이라 루트 시그니처가 다름 (비트 10).
	const uint32_t key = static_cast<uint32_t>(vertexFormat) | (mirrored ? 1u << 8 : 0u) | (material.doubleSided ? 1u << 9 : 0u) | (alphaTest ? 1u << 10 : 0u) | (noBias ? 1u << 11 : 0u);
	auto found = m_shadowPipelines.find(key);
	if (found != m_shadowPipelines.end()) return found->second;

	const bool skinned = vertexFormat == VertexFormat::PositionColorNormalTexcoordTangentSkin;
	PipelineStateDesc desc = m_passDesc[alphaTest ? (skinned ? Pass_ShadowAlphaSkin : Pass_ShadowAlpha) : (skinned ? Pass_ShadowSkin : Pass_Shadow)];
	desc.vertexLayout = GetVertexLayout(vertexFormat);
	desc.rasterizer.frontCounterClockwise = mirrored ? !desc.rasterizer.frontCounterClockwise : desc.rasterizer.frontCounterClockwise;
	desc.rasterizer.cull = material.doubleSided ? CullMode::None : CullMode::Back;
	if (noBias)
	{
		desc.rasterizer.depthBias = 0;
		desc.rasterizer.depthBiasClamp = 0.0f;
		desc.rasterizer.slopeScaledDepthBias = 0.0f;
	}
	const PipelineHandle handle = m_device->CreatePipeline(desc);
	if (handle.IsValid())
	{
		m_shadowPipelines.emplace(key, handle);
	}
	return handle;
}

PipelineHandle Renderer::GetSkyPipeline()
{
	if (m_skyPipeline.IsValid()) return m_skyPipeline;
	// 메인 패스와 같은 타깃에 그리므로 RTV/DSV 포맷도 메인 PSO 와 같게 맞춤 (D3D12 PSO 는 이 값이 다르면 그릴 수 없음).
	// 와이어프레임 설정은 따르지 않음 — 화면을 덮는 삼각형 하나의 모서리만 보여 의미가 없음.
	PipelineStateDesc desc = m_passDesc[Pass_Sky];
	desc.rtvFormats[0] = Format::R16G16B16A16_FLOAT;   // HDR: 메인 PSO 와 같은 FP16 타깃
	desc.rtvFormats[1] = kVelocityFormat;              // 속도 (하늘은 카메라 회전만으로 움직임 — SkyboxPixelShader)
	desc.rtvCount = 2;
	desc.dsvFormat = Format::D24_UNORM_S8_UINT;
	m_skyPipeline = m_device->CreatePipeline(desc);
	return m_skyPipeline;
}

PipelineHandle Renderer::GetTonemapPipeline()
{
	PipelineHandle& cached = m_tonemapPipelines[m_settings.srgbOutput ? 1 : 0];
	if (cached.IsValid()) return cached;
	// 톤 패스는 최종 출력 타깃(에디터는 씬 뷰 텍스처, 게임은 백버퍼)에 그림. RTV 포맷은 그 타깃의 뷰와 같아야
	// 함 (D3D12 PSO). 깊이 없음 — dsvFormat 은 Pass_Tonemap 의 Unknown 그대로 둠.
	PipelineStateDesc desc = m_passDesc[Pass_Tonemap];
	desc.rtvFormats[0] = m_settings.srgbOutput ? Format::R8G8B8A8_UNORM_SRGB : Format::R8G8B8A8_UNORM;
	desc.rtvCount = 1;
	cached = m_device->CreatePipeline(desc);
	return cached;
}

ComputePipelineHandle Renderer::GetComputePipeline(ComputePassId pass)
{
	ComputePipelineHandle& cached = m_computePipelines[pass];
	if (cached.IsValid() || m_computeTried[pass]) return cached;
	m_computeTried[pass] = true;   // 실패(컴퓨트 미지원 백엔드)해도 매 프레임 다시 묻지 않음. 핫리로드(BuildPassDescs)가 되돌림
	const ComputePassInfo& info = kComputePasses[pass];
	ComputePipelineDesc desc;
	desc.cs = m_shaders[info.cs];
	for (uint32_t i = 0; i < info.layoutCount; ++i)
	{
		desc.bindingLayouts[i] = m_layouts[info.layouts[i]];
	}
	desc.bindingLayoutCount = static_cast<uint8_t>(info.layoutCount);
	cached = m_device->CreateComputePipeline(desc);
	return cached;
}

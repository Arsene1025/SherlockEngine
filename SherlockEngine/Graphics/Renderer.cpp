#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/ShaderCompiler.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "Graphics/ShaderConstants.h"
#include "Graphics/VertexTypes.h"
#include "Graphics/Frustum.h"
#include "Graphics/DebugDraw.h"
#include "Graphics/Mesh.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
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
	const wchar_t* file;    // Shaders/ 의 파일 이름
	const char* entry;      // 진입점
	ShaderStage stage;
	const char* name;       // 디버그 이름 (로그, Debug Layer)
};

const Renderer::ShaderInfo Renderer::kShaders[Renderer::Shader_Count] =
{
	{ L"BasicVertexShader.hlsl",   "VS_Main",    ShaderStage::Vertex, "BasicVS" },
	{ L"BasicPixelShader.hlsl",    "PS_Main",    ShaderStage::Pixel,  "BasicPS" },
	{ L"ShadowVertexShader.hlsl",  "VS_Shadow",  ShaderStage::Vertex, "ShadowVS" },
	{ L"SkyboxVertexShader.hlsl",  "VS_Sky",     ShaderStage::Vertex, "SkyboxVS" },
	{ L"SkyboxPixelShader.hlsl",   "PS_Sky",     ShaderStage::Pixel,  "SkyboxPS" },
	// 화면을 덮는 삼각형 하나를 그림. 톤매핑·블러·블룸의 실제 처리는 각 PS 가 함.
	{ L"ScreenVertexShader.hlsl",  "VS_Screen",  ShaderStage::Vertex, "ScreenVS" },
	{ L"TonemapPixelShader.hlsl",  "PS_Tonemap", ShaderStage::Pixel,  "ToneMapPS" },
	{ L"PostPixelShader.hlsl",     "PS_Post",    ShaderStage::Pixel,  "PostPS" },   // 블러·블룸
	{ L"BasicSkinVertexShader.hlsl",  "VS_MainSkin",   ShaderStage::Vertex, "BasicSkinVS" },    // 스키닝 메시. PS 는 BasicPS 를 같이 씀
	{ L"ShadowSkinVertexShader.hlsl", "VS_ShadowSkin", ShaderStage::Vertex, "ShadowSkinVS" },
	{ L"DebugLineVertexShader.hlsl",  "VS_Line",       ShaderStage::Vertex, "DebugLineVS" },    // 본 표시 등 디버그 선
	{ L"DebugLinePixelShader.hlsl",   "PS_Line",       ShaderStage::Pixel,  "DebugLinePS" },
};

struct Renderer::PassInfo
{
	const char* name;
	uint32_t vs;                          // ShaderId
	uint32_t ps;                          // ShaderId. Shader_Count = 픽셀 셰이더 없음 (깊이 전용)
	uint32_t layouts[kMaxBindingSets];    // LayoutId. 순서가 곧 셋 인덱스 (kSetFrame 등)
	uint32_t layoutCount;
};

const Renderer::PassInfo Renderer::kPasses[Renderer::Pass_Count] =
{
	{ "Main",    Shader_BasicVS,  Shader_BasicPS,   { Layout_Frame, Layout_Object, Layout_Material }, 3 },
	{ "Shadow",  Shader_ShadowVS, Shader_Count,     { Layout_Frame, Layout_Object },                  2 },
	{ "Sky",     Shader_SkyVS,    Shader_SkyPS,     { Layout_Frame },                                 1 },
	{ "Tonemap", Shader_ScreenVS, Shader_TonemapPS, { Layout_Post },                                  1 },
	{ "Post",    Shader_ScreenVS, Shader_PostPS,    { Layout_Post },                                  1 },
	{ "MainSkin",   Shader_BasicSkinVS,  Shader_BasicPS, { Layout_Frame, Layout_Object, Layout_Material }, 3 },
	{ "ShadowSkin", Shader_ShadowSkinVS, Shader_Count,   { Layout_Frame, Layout_Object },                  2 },
	{ "DebugLine",  Shader_DebugLineVS,  Shader_DebugLinePS, { Layout_Frame },                             1 },
};

namespace
{
	constexpr const wchar_t* kCommonShaderFile = L"Common.hlsli";   // 모든 셰이더가 include 함. 핫리로드 감시 대상
	constexpr float kHotReloadPollInterval = 0.5f;   // 초
	constexpr uint32_t kInitialLightCapacity = 64;   // 조명 구조체 버퍼의 처음 원소 수 (5,120바이트)

	// HLSL cbuffer 이름 ↔ C++ 구조체 크기. 셰이더가 선언한 cbuffer 는 전부 여기 있어야 함 (없으면 로드 실패).
	struct ConstantBufferInfo { const char* name; uint32_t size; };
	constexpr ConstantBufferInfo kConstantBuffers[] =
	{
		{ "PerFrame",   sizeof(PerFrameConstants) },
		{ "PerObject",  sizeof(PerObjectConstants) },
		{ "Lights",     sizeof(LightConstants) },
		{ "Material",   sizeof(MaterialConstants) },
		{ "PostParams", sizeof(PostParamsConstants) },
		{ "ShadowPass", sizeof(ShadowPassConstants) },
		{ "Shadows",    sizeof(ShadowConstants) },
		{ "Skin",       sizeof(SkinConstants) },
	};

	// 레지스터 문자. 텍스처와 구조체 버퍼는 같은 t# 공간을 씀.
	char RegisterClassOf(BindingType type)
	{
		switch (type)
		{
		case BindingType::ConstantBuffer: return 'b';
		case BindingType::Sampler:        return 's';
		default:                          return 't';
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
	m_skinCB = makeCB(sizeof(SkinConstants), "CB_Skin");   // b7
	m_skinScratch.resize(kMaxSkinJoints);
	m_debugLineVB = m_device->CreateBuffer(BufferDesc{ .size = DebugDraw::kMaxVertices * static_cast<uint32_t>(sizeof(DebugLineVertex)), .usage = BufferUsage::Dynamic,
		.bindFlags = BufferBind_Vertex, .stride = sizeof(DebugLineVertex), .debugName = "VB_DebugLines" });
	m_lightCB = makeCB(sizeof(LightConstants), "CB_Lights");
	// 조명 배열은 상수버퍼가 아니라 구조체 버퍼 (t15). 처음에는 kInitialLightCapacity 개 자리, 모자라면
	// UploadLights 가 두 배로 다시 만듦.
	m_lightCapacity = kInitialLightCapacity;
	m_lightBuffer = m_device->CreateBuffer(BufferDesc{ .size = m_lightCapacity * static_cast<uint32_t>(sizeof(GpuLight)), .usage = BufferUsage::Dynamic,
		.bindFlags = BufferBind_ShaderResource, .stride = sizeof(GpuLight), .debugName = "SB_Lights" });
	m_postParamsCB = makeCB(sizeof(PostParamsConstants), "CB_PostParams");
	m_shadowPassCB = makeCB(sizeof(ShadowPassConstants), "CB_ShadowPass");   // b5
	m_shadowsCB = makeCB(sizeof(ShadowConstants), "CB_Shadows");             // b6
	if (!m_perFrameCB.IsValid() || !m_perObjectCB.IsValid() || !m_skinCB.IsValid() || !m_debugLineVB.IsValid() || !m_lightCB.IsValid() || !m_lightBuffer.IsValid() || !m_postParamsCB.IsValid() ||
		!m_shadowPassCB.IsValid() || !m_shadowsCB.IsValid())
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
		.bindings = { { .buffer = m_perObjectCB }, { .buffer = m_skinCB } },   // 레이아웃의 슬롯 순서: b1, b7
		.debugName = "Set_Object",
	};
	m_objectSet = m_device->CreateResourceSet(objectSet);

	if (!m_frameSet.IsValid() || !m_objectSet.IsValid())
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
		!GetPostPipeline(true).IsValid())
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

	BindingLayoutDesc& objectLayout = m_layoutDescs[Layout_Object];
	objectLayout = BindingLayoutDesc{};
	objectLayout.debugName = "Layout_Object";
	objectLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex, kCBSlotPerObject);
	objectLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex, kCBSlotSkin);   // 스키닝 VS 만 읽음. 다른 VS 는 선언만 해 두고 안 씀

	// 재질 셋: 상수(b3, uvScale 때문에 VS도), 알베도 t0, 노멀 t1, 금속성/거칠기 t2, AO t3, 발광 t4, 샘플러 s0.
	BindingLayoutDesc& materialLayout = m_layoutDescs[Layout_Material];
	materialLayout = BindingLayoutDesc{};
	materialLayout.debugName = "Layout_Material";
	materialLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kCBSlotMaterial);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotAlbedo);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotNormal);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotMetallicRoughness);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotOcclusion);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotEmissive);
	materialLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotMaterial);

	// 후처리 셋: b0 (PerFrame: 노출·톤매핑·디버그 뷰), b4 (PostParams: 패스 파라미터), t13 (입력 텍스처), s6
	// (선형 Clamp). 슬롯 순서가 CreatePostSet 의 bindings 순서임. 모든 후처리 패스(톤매핑·블러·블룸)가 이
	// 레이아웃 하나를 씀 — 셰이더가 안 쓰는 슬롯도 항상 유효한 뷰가 꽂혀 있으므로 D3D12 디스크립터 테이블에도
	// 안전함.
	BindingLayoutDesc& postLayout = m_layoutDescs[Layout_Post];
	postLayout = BindingLayoutDesc{};
	postLayout.debugName = "Layout_Post";
	postLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotPerFrame);   // b0
	postLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotPostParams); // b4
	postLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotSceneHdr);  // t13
	postLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotPost);         // s6

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
		for (uint32_t i = 0; i < info.layoutCount; ++i) desc.bindingLayouts[i] = m_layouts[info.layouts[i]];
		desc.bindingLayoutCount = static_cast<uint8_t>(info.layoutCount);
		m_passDesc[p] = desc;
	}

	// 나머지(깊이 테스트 켬, 불투명, 트라이앵글 리스트)는 Desc 기본값. 패스마다 다른 부분만 적음.
	// 메인 패스: 재질·설정에 따라 달라지는 필드(fill, cull, 정점 레이아웃, 타깃 포맷)는 GetPipelineFor 가 채움.

	// 그림자 패스: VS 만(ps 는 빈 핸들), 렌더 타깃 없음, 깊이 D32_FLOAT. 래스터라이저 깊이 바이어스로 자기
	// 그림자(acne)를 줄임. D32_FLOAT 에서 DepthBias 단위는 2^(지수−23) — z ≈ 0.5 면 한 단위가 6e-8 임.
	for (PassId id : { Pass_Shadow, Pass_ShadowSkin })
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

	// 톤 패스와 블러·블룸: 풀스크린 삼각형, 깊이 없음 (dsvFormat 이 Unknown 이어야 깊이 없는 패스에 쓸 수 있음).
	for (PassId id : { Pass_Tonemap, Pass_Post })
	{
		PipelineStateDesc& desc = m_passDesc[id];
		desc.rasterizer.cull = CullMode::None;
		desc.depthStencil.depthEnable = false;
		desc.depthStencil.depthWrite = false;
		desc.dsvFormat = Format::Unknown;
	}

	// 가산 버전은 블룸 합성용: 출력 = 셰이더 출력 + 기존 값 (알파는 기존 값 유지).
	m_postAddDesc = m_passDesc[Pass_Post];
	m_postAddDesc.blend.rt[0] = RenderTargetBlendDesc{
		.blendEnable = true,
		.srcColor = BlendFactor::One, .dstColor = BlendFactor::One, .colorOp = BlendOp::Add,
		.srcAlpha = BlendFactor::Zero, .dstAlpha = BlendFactor::One, .alphaOp = BlendOp::Add,
	};

	// Desc 가 바뀌었으므로 (새 셰이더 핸들) 작은 키 → PSO 핸들 캐시는 전부 무효.
	m_mainPipelines.clear();
	m_shadowPipelines.clear();
	m_skyPipeline = PipelineHandle{};
	m_debugLinePipeline = PipelineHandle{};
	for (PipelineHandle& handle : m_tonemapPipelines) handle = PipelineHandle{};
	for (PipelineHandle& handle : m_postPipelines) handle = PipelineHandle{};
}

bool Renderer::CreateSamplers()
{
	// SamplerPreset 순서와 같아야 함.
	struct PresetDesc { SamplerFilter filter; SamplerAddress address; float maxLod; const char* name; };
	const PresetDesc presets[kSamplerPresetCount] =
	{
		{ SamplerFilter::Linear,      SamplerAddress::Wrap,  1000.0f, "Sampler_LinearWrap" },
		{ SamplerFilter::Linear,      SamplerAddress::Clamp, 1000.0f, "Sampler_LinearClamp" },
		{ SamplerFilter::Anisotropic, SamplerAddress::Wrap,  1000.0f, "Sampler_AnisotropicWrap" },
		{ SamplerFilter::Point,       SamplerAddress::Wrap,  1000.0f, "Sampler_PointWrap" },
		{ SamplerFilter::Point,       SamplerAddress::Wrap,  0.0f,    "Sampler_PointNoMip" },   // maxLod 0 = 밉 0만
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
	if (sky.IsValid()) binding.skybox = sky;

	// 반사: 프리필터 큐브 → 없으면 스카이박스의 밉 체인으로 근사(박스 필터라 GGX 로 거른 것보다 거칠기 모양이 덜 맞음).
	if (specular.IsValid()) { binding.specular = specular; binding.specularMips = specularMips; }
	else if (sky.IsValid()) { binding.specular = sky; binding.specularMips = skyMips; }
	// 확산: 조도 큐브 → 없으면 스카이박스(또는 반사 큐브)의 끝에서 두 번째 밉. 면당 2×2 라 위·아래·옆의 밝기
	// 차이 정도만 남음.
	if (irradiance.IsValid()) { binding.irradiance = irradiance; binding.irradianceLod = 0.0f; }
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
		if (lut != m_textures.GetWhite()) { binding.brdfLut = lut; binding.hasBrdfLut = true; }
	}

	binding.iblIntensity = environment.iblIntensity;
	binding.exposure = environment.exposure;
	binding.toneMapping = environment.toneMapping;
	m_environment = binding;
	m_postSettings = environment.post;
}

bool Renderer::UpdateFrameSet()
{
	if (m_frameSet.IsValid() && m_environment.SameTextures(m_boundEnvironment)) return true;

	// ResourceSet 은 값 객체라 슬롯 하나만 바꾸는 기능이 없음 — 재질 셋과 같이 통째로 다시 만듦 (D3D12
	// 디스크립터 테이블도 다시 씀). 이전 셋은 D3D12 에서 지연 해제되므로, 이미 기록된 드로우가 그 테이블을
	// 읽어도 안전함.
	m_device->DestroyResourceSet(m_frameSet);
	// bindings[i] 는 CreateLayouts 의 frameLayout.Add(...) i 번째 슬롯과 짝임: b0, b2, t8, s4, t9, s5, t10, t11,
	// t12, b5, b6, t14, t15. 환경 샘플러는 재질 프리셋의 LinearClamp 를 함께 씀 (큐브 샘플은 면 경계를 하드웨어가
	// 이어 주고, LUT 는 가장자리에서 반복되면 안 됨).
	const ResourceSetDesc frameSet{
		.layout = m_layouts[Layout_Frame],
		.bindings = {
			{ .buffer = m_perFrameCB }, { .buffer = m_lightCB }, { .texture = m_shadowMap }, { .sampler = m_shadowSampler },
			{ .texture = m_environment.skybox }, { .sampler = GetSampler(SamplerPreset::LinearClamp) },
			{ .texture = m_environment.irradiance }, { .texture = m_environment.specular }, { .texture = m_environment.brdfLut },
			{ .buffer = m_shadowPassCB }, { .buffer = m_shadowsCB }, { .texture = m_csmMap }, { .buffer = m_lightBuffer },
		},
		.debugName = "Set_Frame",
	};
	m_frameSet = m_device->CreateResourceSet(frameSet);
	m_boundEnvironment = m_environment;
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
				if (buffer.name == info.name) { expected = &info; break; }
			}
			if (expected == nullptr)
			{
				Log::Error("%s : cbuffer '%s' 가 kConstantBuffers 표에 없음. C++ 구조체와 함께 표에 추가할 것.", kShaders[i].name, buffer.name.c_str());
				layoutOk = false;
			}
			else if (buffer.size != expected->size)
			{
				Log::Error("%s : cbuffer '%s' 크기 불일치: HLSL %u 바이트, C++ %u 바이트. 패킹 규칙(float3 뒤 스칼라)을 확인할 것.",
					kShaders[i].name, buffer.name.c_str(), buffer.size, expected->size);
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
		for (ShaderHandle handle : created) m_device->DestroyShader(handle);
		return false;
	}

	// 이전 셰이더는 PSO가 참조로 붙들고 있으므로 풀에서 지워도 캐시가 비워질 때까지 살아 있음.
	for (uint32_t i = 0; i < Shader_Count; ++i)
	{
		m_device->DestroyShader(m_shaders[i]);
		m_shaders[i] = created[i];
	}

	// 핫리로드 감시 목록: 표의 소스 파일 전부 + Common.hlsli. Release에서도 목록은 만들지만 CheckHotReload가
	// 동작하지 않음.
	m_watched.clear();
	auto watch = [this](const wchar_t* file)
	{
		WatchedFile watched;
		watched.path = Paths::GetShaderSourcePath(file);
		for (const WatchedFile& existing : m_watched)
		{
			if (existing.path == watched.path) return;   // 같은 파일을 두 셰이더가 쓰는 경우
		}
		watched.writeTime = ShaderCompiler::GetLastWriteTime(watched.path);
		m_watched.push_back(watched);
	};
	for (const ShaderInfo& info : kShaders) watch(info.file);
	watch(kCommonShaderFile);

	std::string loaded;
	for (uint32_t i = 0; i < Shader_Count; ++i)
	{
		if (!loaded.empty()) loaded += ", ";
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
							Log::Error("바인딩 대조: [%s] %s 와 %s 가 같은 레지스터 %c%u 를 같은 스테이지에 선언함.",
								pass.name, la.debugName, lb.debugName, RegisterClassOf(x.type), x.reg);
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
						if (slot.type == u.type && slot.reg == u.reg && (slot.stageMask & stage)) { found = true; break; }
					}
				}
				const char typeName = RegisterClassOf(u.type);
				if (!found)
				{
					Log::Error("바인딩 대조: [%s] %s가 %c%u ('%s')를 쓰지만 이 패스의 BindingLayout에 그 스테이지로 선언되지 않음.",
						pass.name, kShaders[shader].name, typeName, u.reg, u.name.c_str());
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
		for (WatchedFile& w : m_watched) w.writeTime = ShaderCompiler::GetLastWriteTime(w.path);
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
		if (now != 0 && now != w.writeTime) { changed = true; break; }
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
	m_frameSet = m_objectSet = ResourceSetHandle{};
	for (BindingLayoutHandle& layout : m_layouts)
	{
		m_device->DestroyBindingLayout(layout);
		layout = BindingLayoutHandle{};
	}
	for (BufferHandle* buffer : { &m_perFrameCB, &m_perObjectCB, &m_skinCB, &m_debugLineVB, &m_lightCB, &m_lightBuffer, &m_postParamsCB, &m_shadowPassCB, &m_shadowsCB })
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

void Renderer::InvalidateScene(const Scene& scene, bool releaseTextures)
{
	for (const auto& mesh : scene.GetMeshes()) InvalidateMesh(mesh.get());
	for (const auto& material : scene.GetMaterials()) InvalidateMaterial(material.get());
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
	if (m_device == nullptr) return;

	CheckHotReload(totalTime);
	// 그릴 타깃(HDR·깊이)이 있어야 함. 에디터는 SetSceneTarget 이 이미 만들었고, 게임 모드는 출력 크기에 맞춰
	// 여기서 만듦. 만들지 못했으면 이번 프레임은 건너뜀.
	if (!IsOffscreen()) EnsureGameTarget();
	if (!m_mainView.IsValid()) return;
	// 단계별 썸네일. 창이 요청한 프레임에만 (요청은 한 번 쓰면 지움 — 창을 닫으면 다음 프레임부터 멈춤).
	m_postCapture = m_postCaptureRequested && IsOffscreen();
	m_postCaptureRequested = false;
	for (PostStageThumb& thumb : m_stageThumbs) thumb.active = false;
	if (m_postCapture) EnsureStageThumbs(m_mainView.width, m_mainView.height);
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

	// ---- 조명: 씬의 Light → GPU 배치. BuildShadowViews 가 shadowIndex 를 채우므로 올리기(UploadLights) 전에 만듦 ----
	BuildGpuLights(scene);

	// ---- 그림자 뷰: 조명마다 아틀라스 타일을 배정하고 shadowIndex 를 채움 → b6(+b5) → b0 (shadowParams.w 가 이 결과를 씀) ----
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

	// ---- b0 PerFrame ----
	UploadPerFrameConstants(camera, totalTime);

	// ---- b2 Lights + t15 조명 배열 ----
	UploadLights(scene);

	BuildDrawList(scene);
	UploadDebugLines(debugDraw);   // 디버그 선(DebugDraw 가 모은 것)을 동적 정점 버퍼에 올림. 프레임마다 한 번은 써야 함

	m_device->EndResourceUploads();

	// 환경 텍스처를 프레임 셋 t9~t12 에 꽂음. 없는 것도 슬롯을 비우지 않고 검은 큐브·흰 LUT 를 꽂음
	// (D3D12 디스크립터 테이블에는 유효한 뷰가 있어야 함. 셰이더는 environmentParams·outputParams 의 플래그로
	// 쓸지 정함).
	UpdateFrameSet();

	// 프레임 셋(b0, b2, t8, s4, t9, s5, t10~t12)과 오브젝트 셋(b1)을 바인딩함. 그림자 패스는 t8(그림자 맵)을
	// DSV 로 쓰므로 BeginRenderPass 가 t8 SRV 바인딩을 해제함. 씬 패스에서 셋을 다시 바인딩해 t8 을 복구함.
	cmd.SetResourceSet(kSetFrame, m_frameSet);
	cmd.SetResourceSet(kSetObject, m_objectSet);

	if (m_shadowEnabledThisFrame)
	{
		GpuProfileScope gpuScope(cmd, "ShadowPass");
		RenderShadowPass(cmd);
	}
	else
	{
		// 그림자 패스가 없는 프레임에도 프레임 셋은 t8 / t14 를 SRV 로 가리킴. 상태를 맞춰 둠 (이미 그 상태면 아무 일도 없음).
		cmd.Barrier(m_shadowMap, ResourceState::ShaderResource);
		cmd.Barrier(m_csmMap, ResourceState::ShaderResource);
	}
	{
		GpuProfileScope gpuScope(cmd, "MainPass");
		RenderScenePass(cmd, scene, m_mainView, camera, "MainPass");
	}
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
		RenderTonemapPass(cmd, m_mainView.hdrSet, output, after, "TonemapPass",
			m_mainView.width, m_mainView.height, m_mainView.width, m_mainView.height);
	}
	if (m_previewCamera != nullptr && m_previewView.IsValid() && m_previewView.color.IsValid())
	{
		// 선택한 카메라 오브젝트의 시점. b0 만 그 카메라로 바꿔 같은 드로우 목록을 작은 타깃에 다시 그림
		// (컬링은 그 카메라의 절두체로 다시 함).
		GpuProfileScope gpuScope(cmd, "PreviewPass");
		UploadPerFrameConstants(*m_previewCamera, totalTime);
		RenderScenePass(cmd, scene, m_previewView, *m_previewCamera, "PreviewPass");
		ApplyHdrEffects(cmd, m_previewView, false);
		RenderTonemapPass(cmd, m_previewView.hdrSet, m_previewView.color, ResourceState::ShaderResource, "PreviewTonemapPass",
			m_previewView.width, m_previewView.height, m_previewView.width, m_previewView.height);
	}
}

void Renderer::BuildGpuLights(const Scene& scene)
{
	const std::vector<Light>& sceneLights = scene.GetLights();
	m_gpuLights.clear();   // 용량은 남음 — 프레임마다 다시 할당하지 않음
	m_gpuLights.reserve(sceneLights.size());
	for (const Light& light : sceneLights)
	{
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

bool Renderer::UploadLights(const Scene& scene)
{
	const uint32_t count = static_cast<uint32_t>(m_gpuLights.size());
	if (count > m_lightCapacity)
	{
		// 자리가 모자람: 두 배씩 늘려 다시 만듦. 프레임 셋(t15)이 옛 버퍼를 가리키므로 바인딩 기록을 비워
		// 다음 UpdateFrameSet 이 셋을 다시 만들게 함. 옛 버퍼는 D3D12 에서 지연 해제됨.
		uint32_t capacity = m_lightCapacity > 0 ? m_lightCapacity : kInitialLightCapacity;
		while (capacity < count) capacity *= 2;
		const BufferHandle grown = m_device->CreateBuffer(BufferDesc{ .size = capacity * static_cast<uint32_t>(sizeof(GpuLight)), .usage = BufferUsage::Dynamic,
			.bindFlags = BufferBind_ShaderResource, .stride = sizeof(GpuLight), .debugName = "SB_Lights" });
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
			if (!warned) { warned = true; Log::Error("조명 버퍼 확장 실패 (%u개). 앞의 %u개만 그림.", count, m_lightCapacity); }
		}
	}
	const uint32_t uploaded = (std::min)(count, m_lightCapacity);

	// 조명이 0개여도 한 칸은 올림 — D3D12 의 Dynamic 버퍼는 프레임마다 갱신돼야 함 (셰이더는 lightCount 만큼만 읽음).
	static const GpuLight kNoLight = {};
	m_device->UpdateBuffer(m_lightBuffer, uploaded > 0 ? m_gpuLights.data() : &kNoLight,
		(std::max)(uploaded, 1u) * static_cast<uint32_t>(sizeof(GpuLight)));

	LightConstants constants = {};
	constants.ambientColor = scene.ambientColor;
	constants.lightCount = uploaded;
	m_device->UpdateBuffer(m_lightCB, &constants, sizeof(constants));
	return uploaded == count;
}

void Renderer::BuildDrawList(const Scene& scene)
{
	m_drawList.clear();
	m_objects.clear();

	// b7 Skin 은 오브젝트 셋에 항상 묶여 있음. D3D12 의 Dynamic 상수버퍼는 프레임마다 한 번은 써야 하므로(안 쓰면
	// DrawIndexed 가 거부함) 스키닝 드로우가 없는 프레임에도 단위행렬 하나로 채워 둠. 스키닝 드로우는 그리기 직전에 다시 씀.
	XMStoreFloat4x4(&m_skinScratch[0], XMMatrixIdentity());
	m_device->UpdateBuffer(m_skinCB, m_skinScratch.data(), static_cast<uint32_t>(sizeof(XMFLOAT4X4)));

	for (const auto& objectPtr : scene.GetObjects())
	{
		const GameObject& object = *objectPtr;   // 씬이 unique_ptr 로 소유함
		const Mesh* mesh = object.GetMesh();
		if (mesh == nullptr || !mesh->IsValid()) continue;
		const GpuMesh* gpuMesh = GetOrCreateGpuMesh(*mesh);
		if (gpuMesh == nullptr) continue;

		// 월드 행렬은 부모 계층을 따라 올라가며 곱하는 값이라 싸지 않음. 오브젝트마다 여기서 한 번만 구하고,
		// 역행렬·경계 구까지 만들어 둠 — 그림자 뷰 N 개 + 메인 + 미리보기 패스가 모두 이 결과를 씀.
		const XMMATRIX world = object.GetWorldMatrix();
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
			if (material == nullptr) material = &m_defaultMaterial;

			DrawItem item;
			item.pipeline = GetPipelineFor(*material, mesh->GetVertexFormat(), mirrored);
			if (!item.pipeline.IsValid()) continue;
			item.gpuMaterial = GetOrCreateGpuMaterial(*material, &scene);
			if (item.gpuMaterial == nullptr) continue;
			// 감마 검증 카드처럼 조명을 받지 않는 물체는 그림자도 만들지 않음 (빈 핸들 = 그림자 패스에서 건너뜀).
			item.shadowPipeline = material->unlit ? PipelineHandle{} : GetShadowPipelineFor(*material, mesh->GetVertexFormat(), mirrored);
			item.material = material;
			item.mesh = mesh;
			item.gpuMesh = gpuMesh;
			item.object = objectIndex;
			item.indexStart = submesh.indexStart;
			item.indexCount = submesh.indexCount;
			m_drawList.push_back(item);
		}
	}

	// ---- 정렬: PSO → Material → Mesh → 인덱스 범위. 같은 상태가 연속되면 바인딩을 건너뛸 수 있음 ----
	std::sort(m_drawList.begin(), m_drawList.end(), [](const DrawItem& a, const DrawItem& b)
	{
		if (a.pipeline.index != b.pipeline.index) return a.pipeline.index < b.pipeline.index;
		if (a.material != b.material) return a.material < b.material;
		if (a.mesh != b.mesh) return a.mesh < b.mesh;
		return a.indexStart < b.indexStart;
	});
}

void Renderer::UploadObjectConstants(uint32_t object)
{
	// b1 PerObject. 드로우마다 Map(WRITE_DISCARD). 드라이버가 매번 새 메모리를 주므로
	// 앞 드로우가 아직 실행되지 않았어도 그 데이터를 덮어쓰지 않음. 바인딩은 프레임 초에 한 번만 했음.
	// 행렬은 BuildDrawList 가 계산해 둔 것 — 여기서는 복사만 함.
	const ObjectData& data = m_objects[object];
	PerObjectConstants perObject;
	perObject.world = data.world;
	perObject.worldInvTranspose = data.worldInvTranspose;
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
			if (j < count) XMStoreFloat4x4(&m_skinScratch[j], XMMatrixTranspose(XMLoadFloat4x4(&(*palette)[j])));
			else XMStoreFloat4x4(&m_skinScratch[j], XMMatrixIdentity());
		}
		m_device->UpdateBuffer(m_skinCB, m_skinScratch.data(), uploaded * static_cast<uint32_t>(sizeof(XMFLOAT4X4)));
	}
}

void Renderer::RenderScenePass(RHI::CommandList& cmd, const Scene& scene, ViewTarget& view, const Camera& camera, const char* debugName)
{
	// HDR: 씬은 선형 FP16 텍스처(view.hdr)에 그림. 최종 출력(씬 뷰 텍스처 또는 백버퍼)과 그 상태 전이는 톤
	// 패스가 맡음. 깊이는 뷰가 가진 텍스처 — 에디터든 게임 모드든 같은 경로임. 패스가 끝나도 DepthWrite 로
	// 두고, 깊이를 읽는 패스(SSAO·디퍼드 라이팅)가 생기면 그 패스가 ShaderResource 로 전이해 읽으면 됨.
	cmd.Barrier(view.hdr, ResourceState::RenderTarget);
	cmd.Barrier(view.depth, ResourceState::DepthWrite);

	const RenderPassDesc pass{
		.colors = { {
			.texture = view.hdr,
			.load = LoadOp::Clear,
			.srgbView = false,   // FP16 에는 sRGB 뷰가 없음. 선형 값을 그대로 씀 — sRGB 인코딩은 톤 패스의 RTV 가 함
			.clearColor = { scene.clearColor[0], scene.clearColor[1], scene.clearColor[2], scene.clearColor[3] },   // 선형. 노출·ACES 를 거침
		} },
		.colorCount = 1,
		.depth = { .texture = view.depth, .load = LoadOp::Clear },
		.debugName = debugName,
	};
	cmd.BeginRenderPass(pass);
	DrawItems(cmd, scene, camera);
	cmd.EndRenderPass();

	// 톤 패스가 t13 으로 읽음.
	cmd.Barrier(view.hdr, ResourceState::ShaderResource);
}

void Renderer::DrawItems(RHI::CommandList& cmd, const Scene& scene, const Camera& camera)
{
	// 그림자 패스에서 t8 바인딩이 해제됐고, 후처리 패스가 셋 인덱스 0 에 후처리 셋을 꽂았을 수 있으므로
	// 프레임 셋을 다시 바인딩함 (이제 그림자 맵은 SRV 상태). 패스마다 새로 바인딩함.
	cmd.SetResourceSet(kSetFrame, m_frameSet);
	cmd.SetResourceSet(kSetObject, m_objectSet);

	// 이 카메라의 절두체. 경계 구가 완전히 밖에 있는 오브젝트는 드로우(상수 업로드 포함)를 건너뜀.
	const Frustum frustum = Frustum::FromViewProj(camera.GetViewMatrix() * camera.GetProjectionMatrix());

	// ---- 드로우. "직전" 상태 기억은 이 패스 안에서만 유효함 ----
	PipelineHandle lastPipeline;
	const GpuMaterial* lastMaterial = nullptr;
	const GpuMesh* lastMesh = nullptr;

	for (const DrawItem& item : m_drawList)
	{
		if (!frustum.Intersects(m_objects[item.object].sphere))
		{
			++m_stats.culled;
			continue;
		}

		if (item.pipeline != lastPipeline)
		{
			cmd.SetPipelineState(item.pipeline);
			lastPipeline = item.pipeline;
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
		UploadObjectConstants(item.object);
		cmd.DrawIndexed(item.indexCount, item.indexStart, 0);
		m_stats.triangles += item.indexCount / 3;
		++m_stats.draws;
	}

	// 불투명 물체를 다 그린 뒤 하늘. 깊이 버퍼가 채워져 있으므로 가려진 픽셀은 깊이 테스트에서 떨어짐.
	if (!scene.environment.skybox.empty())
	{
		DrawSkybox(cmd);
	}

	DrawDebugLines(cmd);   // 하늘 뒤: 깊이 테스트가 없으므로 항상 위에 그려짐
}

void Renderer::UploadDebugLines(const DebugDraw& debugDraw)
{
	// D3D12 의 Dynamic 버퍼는 프레임마다 한 번은 써야 바인딩됨. 선이 없어도 정점 하나 분량을 올리고 그리지는 않음 (m_debugLineCount = 0).
	const std::vector<DebugLineVertex>& vertices = debugDraw.GetVertices();

	m_debugLineCount = static_cast<uint32_t>(vertices.size());
	static const DebugLineVertex kNoLine = {};
	m_device->UpdateBuffer(m_debugLineVB, m_debugLineCount > 0 ? vertices.data() : &kNoLine,
		(std::max)(m_debugLineCount, 1u) * static_cast<uint32_t>(sizeof(DebugLineVertex)));
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

void Renderer::UploadPerFrameConstants(const Camera& camera, float totalTime)
{
	// HLSL은 column_major로 읽고 셰이더는 mul(v, M)을 쓰므로 전치해서 올림 (ShaderConstants.h).
	// 미리보기 패스에서 다른 카메라로 한 번 더 호출함 (업로드 링을 쓰므로 프레임 안에서 여러 번 올려도 됨).
	const XMMATRIX view = camera.GetViewMatrix();
	const XMMATRIX proj = camera.GetProjectionMatrix();
	PerFrameConstants perFrame = {};
	XMStoreFloat4x4(&perFrame.view, XMMatrixTranspose(view));
	XMStoreFloat4x4(&perFrame.proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&perFrame.viewProj, XMMatrixTranspose(view * proj));
	perFrame.cameraPosition = camera.GetPosition();
	perFrame.time = totalTime;
	perFrame.shadowParams = XMFLOAT4(1.0f / kShadowAtlasSize, m_shadowSettings.bias, m_shadowSettings.strength, m_shadowEnabledThisFrame ? 1.0f : 0.0f);
	perFrame.debugParams = XMFLOAT4(static_cast<float>(m_settings.debugView), m_settings.debugDepthRange, 0.0f, 0.0f);
	// ResolveEnvironment 가 이번 프레임에 정한 환경 (Render 맨 앞에서 불림)
	perFrame.environmentParams = XMFLOAT4(m_environment.iblIntensity, static_cast<float>(m_environment.specularMips),
		m_environment.irradianceLod, m_environment.hasEnvironment ? 1.0f : 0.0f);
	perFrame.outputParams = XMFLOAT4(m_environment.exposure, m_environment.toneMapping ? 1.0f : 0.0f, m_environment.hasBrdfLut ? 1.0f : 0.0f, 0.0f);
	perFrame.shadowExtra = XMFLOAT4(m_shadowSettings.normalOffset, 0.0f, 0.0f, 0.0f);
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
		for (size_t i = 0; i < indices16.size(); ++i) indices16[i] = static_cast<uint16_t>(mesh.GetIndices()[i]);
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

	// 셋에 들어가는 입력(텍스처 이름 다섯·알베도 색공간·샘플러)이 셋을 만들 때와 같고 텍스처 캐시가 그 뒤로
	// 비워지지 않았으면, 이름 → 핸들 조회를 건너뜀. 대부분의 프레임이 이 경로임 — 재질 편집(ImGui), 샘플러
	// 덮어쓰기, 씬 교체 때만 아래로 내려감.
	const SamplerPreset preset = (m_settings.samplerOverride >= 0 && m_settings.samplerOverride < static_cast<int>(kSamplerPresetCount))
		? static_cast<SamplerPreset>(m_settings.samplerOverride) : material.sampler;
	const SamplerHandle sampler = GetSampler(preset);
	const bool unchanged = gpuMaterial->set.IsValid()
		&& gpuMaterial->textureGeneration == m_textures.GetGeneration()
		&& gpuMaterial->sampler == sampler
		&& gpuMaterial->albedoSrgb == material.albedoSrgb
		&& gpuMaterial->albedoName == material.albedoTexture
		&& gpuMaterial->normalName == material.normalTexture
		&& gpuMaterial->metallicRoughnessName == material.metallicRoughnessTexture
		&& gpuMaterial->occlusionName == material.occlusionTexture
		&& gpuMaterial->emissiveName == material.emissiveTexture;
	if (!unchanged)
	{
		const TextureHandle albedo = m_textures.Load2D(material.albedoTexture, material.albedoSrgb, scene);
		const TextureHandle normal = material.normalTexture.empty() ? m_textures.GetFlatNormal() : m_textures.Load2D(material.normalTexture, false, scene);
		// PBR: 빈 이름은 흰색 — 금속성/거칠기·AO·발광 모두 계수가 그대로 쓰임. 발광만 색이라 sRGB.
		const TextureHandle metallicRoughness = m_textures.Load2D(material.metallicRoughnessTexture, false, scene);
		const TextureHandle occlusion = m_textures.Load2D(material.occlusionTexture, false, scene);
		const TextureHandle emissive = m_textures.Load2D(material.emissiveTexture, true, scene);

		// 실제로 꽂히는 핸들이 달라졌을 때만 ResourceSet 을 다시 만듦. ResourceSet은 값 객체라 부분 갱신 기능이
		// 없음 — D3D12 디스크립터 테이블도 다시 써야 함.
		if (!gpuMaterial->set.IsValid() || gpuMaterial->boundAlbedo != albedo || gpuMaterial->boundNormal != normal || gpuMaterial->boundSampler != sampler
			|| gpuMaterial->boundMetallicRoughness != metallicRoughness || gpuMaterial->boundOcclusion != occlusion || gpuMaterial->boundEmissive != emissive)
		{
			m_device->DestroyResourceSet(gpuMaterial->set);
			// 슬롯 순서는 CreateLayouts 의 materialLayout.Add(...) 순서: b3 상수, t0 알베도, t1 노멀, t2
			// 금속성/거칠기, t3 AO, t4 발광, s0 샘플러.
			const ResourceSetDesc setDesc{
				.layout = m_layouts[Layout_Material],
				.bindings = {
					{ .buffer = gpuMaterial->constants }, { .texture = albedo }, { .texture = normal },
					{ .texture = metallicRoughness }, { .texture = occlusion }, { .texture = emissive }, { .sampler = sampler },
				},
				.debugName = "Set_Material",
			};
			gpuMaterial->set = m_device->CreateResourceSet(setDesc);
			gpuMaterial->boundAlbedo = albedo;
			gpuMaterial->boundNormal = normal;
			gpuMaterial->boundMetallicRoughness = metallicRoughness;
			gpuMaterial->boundOcclusion = occlusion;
			gpuMaterial->boundEmissive = emissive;
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
		constants.uvScale = material.uvScale;
		constants.unlit = material.unlit ? 1.0f : 0.0f;
		constants.normalStrength = m_settings.normalMapping ? material.normalStrength : 0.0f;
		constants.alphaCutoff = material.alphaCutoff;
		m_device->UpdateBuffer(gpuMaterial->constants, &constants, sizeof(constants));
		gpuMaterial->uploadedFrame = m_frameNumber;
	}
	return gpuMaterial;
}

// ------------------------------------------------------------------ PSO

PipelineHandle Renderer::GetPipelineFor(const Material& material, VertexFormat vertexFormat, bool mirrored)
{
	// 이 PSO 를 가르는 것은 정점 포맷·미러·채우기·컬링 네 가지뿐. 그 비트를 키로 핸들을 기억해 두면, 드로우마다
	// PipelineStateDesc 전체를 복사하고 해시해 Device 캐시를 찾는 일이 없어짐.
	const bool wireframe = m_settings.wireframe || material.wireframe;
	const bool cullBack = m_settings.cullBack && !material.doubleSided;
	const uint32_t key = static_cast<uint32_t>(vertexFormat) | (mirrored ? 1u << 8 : 0u) | (wireframe ? 1u << 9 : 0u) | (cullBack ? 1u << 10 : 0u);
	auto found = m_mainPipelines.find(key);
	if (found != m_mainPipelines.end()) return found->second;

	PipelineStateDesc desc = m_passDesc[vertexFormat == VertexFormat::PositionColorNormalTexcoordTangentSkin ? Pass_MainSkin : Pass_Main];
	desc.vertexLayout = GetVertexLayout(vertexFormat);
	desc.rasterizer.frontCounterClockwise = mirrored ? !desc.rasterizer.frontCounterClockwise : desc.rasterizer.frontCounterClockwise;
	desc.rasterizer.fill = wireframe ? FillMode::Wireframe : FillMode::Solid;
	desc.rasterizer.cull = cullBack ? CullMode::Back : CullMode::None;
	// D3D12 PSO 는 렌더 타깃 뷰의 포맷을 알아야 함. 메인 패스는 FP16 HDR 텍스처에 그리므로 항상 R16G16B16A16_FLOAT.
	// (sRGB 여부는 톤 패스의 PSO 만 따짐 — GetTonemapPipeline)
	desc.rtvFormats[0] = Format::R16G16B16A16_FLOAT;
	desc.rtvCount = 1;
	desc.dsvFormat = Format::D24_UNORM_S8_UINT;
	const PipelineHandle handle = m_device->CreatePipeline(desc);
	if (handle.IsValid()) m_mainPipelines.emplace(key, handle);   // 실패는 기억하지 않음 (다음에 다시 시도)
	return handle;
}

PipelineHandle Renderer::GetShadowPipelineFor(const Material& material, VertexFormat vertexFormat, bool mirrored)
{
	// 그림자는 항상 솔리드로 그림 (와이어프레임 재질도 실체는 있음). 양면 재질은 컬링 없이 그림.
	const uint32_t key = static_cast<uint32_t>(vertexFormat) | (mirrored ? 1u << 8 : 0u) | (material.doubleSided ? 1u << 9 : 0u);
	auto found = m_shadowPipelines.find(key);
	if (found != m_shadowPipelines.end()) return found->second;

	PipelineStateDesc desc = m_passDesc[vertexFormat == VertexFormat::PositionColorNormalTexcoordTangentSkin ? Pass_ShadowSkin : Pass_Shadow];
	desc.vertexLayout = GetVertexLayout(vertexFormat);
	desc.rasterizer.frontCounterClockwise = mirrored ? !desc.rasterizer.frontCounterClockwise : desc.rasterizer.frontCounterClockwise;
	desc.rasterizer.cull = material.doubleSided ? CullMode::None : CullMode::Back;
	const PipelineHandle handle = m_device->CreatePipeline(desc);
	if (handle.IsValid()) m_shadowPipelines.emplace(key, handle);
	return handle;
}

PipelineHandle Renderer::GetSkyPipeline()
{
	if (m_skyPipeline.IsValid()) return m_skyPipeline;
	// 메인 패스와 같은 타깃에 그리므로 RTV/DSV 포맷도 메인 PSO 와 같게 맞춤 (D3D12 PSO 는 이 값이 다르면 그릴 수 없음).
	// 와이어프레임 설정은 따르지 않음 — 화면을 덮는 삼각형 하나의 모서리만 보여 의미가 없음.
	PipelineStateDesc desc = m_passDesc[Pass_Sky];
	desc.rtvFormats[0] = Format::R16G16B16A16_FLOAT;   // HDR: 메인 PSO 와 같은 FP16 타깃
	desc.rtvCount = 1;
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

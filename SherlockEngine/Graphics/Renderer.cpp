#include "pch.h"
#include "Graphics/Renderer.h"
#include "RHI/Device.h"
#include "RHI/CommandList.h"
#include "RHI/ShaderCompiler.h"
#include "RHI/ResourceDesc.h"
#include "RHI/BindingTypes.h"
#include "Graphics/ShaderConstants.h"
#include "Graphics/VertexTypes.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/Mesh.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include "Core/Profiler.h"   // GPU 구간
#include <algorithm>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	constexpr const wchar_t* kVertexShaderFile = L"BasicVertexShader.hlsl";
	constexpr const wchar_t* kPixelShaderFile = L"BasicPixelShader.hlsl";
	constexpr const wchar_t* kShadowShaderFile = L"ShadowVertexShader.hlsl";
	constexpr const wchar_t* kSkyVertexShaderFile = L"SkyboxVertexShader.hlsl";
	constexpr const wchar_t* kSkyPixelShaderFile = L"SkyboxPixelShader.hlsl";
	constexpr const wchar_t* kCommonShaderFile = L"Common.hlsli";

	// Screen에 화면을 덮는 거대한 삼각형을 그림
	// 실제 처리는 각 PixelShader에서 하며 Tonemap, Bloom등의 실제 처리는 PS에서 함.
	constexpr const wchar_t* kScreenVertexShaderFile = L"ScreenVertexShader.hlsl";

	// PP
	constexpr const wchar_t* kTonemapPixelShaderFile = L"TonemapPixelShader.hlsl";
	constexpr const wchar_t* kPostPixelShaderFile = L"PostPixelShader.hlsl";   // 블러·블룸 (PS_Post)

	//
	constexpr float kHotReloadPollInterval = 0.5f;   // 초

	std::wstring ToWide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
		return wide;
	}

	// ---- 섀도 아틀라스 배치 ----
	// 메인 아틀라스 (2048², 지역광 전용): 512² 타일 16개, 4×4. 타일 번호는 왼→오, 위→아래.
	//   ┌────┬────┬────┬────┐
	//   │ 0  │ 1  │ 2  │ 3  │
	//   ├────┼────┼────┼────┤   스팟 = 1타일, 포인트 = 연속 6타일 (행을 넘어가도 됨 — 뷰마다 사각형을 따로 가짐)
	//   │ 4  │ 5  │ 6  │ 7  │
	//   ├────┼────┼────┼────┤
	//   │ 8  │ 9  │ 10 │ 11 │
	//   ├────┼────┼────┼────┤
	//   │ 12 │ 13 │ 14 │ 15 │
	//   └────┴────┴────┴────┘
	// 방향광은 모두 CSM 전용 아틀라스(CsmRect, CsmSingleRect)에 그림.
	struct AtlasRect { uint32_t x, y, size; };

	// 방향광 단일 뷰(캐스케이드 1)는 CSM 아틀라스 전체를 씀.
	AtlasRect CsmSingleRect()
	{
		return { 0, 0, Renderer::kCsmAtlasWidth };
	}

	// CSM 전용 아틀라스 (2048×2048)의 캐스케이드 칸. 크기는 Renderer::kCsmCascadeSize. 배치를 바꾸면 헤더의
	// 그림도 같이 고칠 것.
	//   C0 = (0, 0), C1 = (1024, 0), C2 = (0, 1024), C3 = (1024, 1024) — 모두 1024
	AtlasRect CsmRect(uint32_t index)
	{
		index = (std::min)(index, 3u);
		static constexpr uint32_t kX[4] = { 0, 1024, 0, 1024 };
		static constexpr uint32_t kY[4] = { 0, 0, 1024, 1024 };
		return { kX[index], kY[index], Renderer::kCsmCascadeSize[index] };
	}

	// 지역광 타일 0..15. 메인 아틀라스의 4×4 격자 (왼→오, 위→아래).
	AtlasRect LocalTileRect(uint32_t tile)
	{
		constexpr uint32_t kColumns = Renderer::kShadowAtlasSize / Renderer::kShadowTileSize;
		tile = (std::min)(tile, Renderer::kShadowLocalTileCount - 1);
		return { (tile % kColumns) * Renderer::kShadowTileSize, (tile / kColumns) * Renderer::kShadowTileSize, Renderer::kShadowTileSize };
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
	m_lightCB = makeCB(sizeof(LightConstants), "CB_Lights");
	m_postParamsCB = makeCB(sizeof(PostParamsConstants), "CB_PostParams");
	m_shadowPassCB = makeCB(sizeof(ShadowPassConstants), "CB_ShadowPass");   // b5
	m_shadowsCB = makeCB(sizeof(ShadowConstants), "CB_Shadows");             // b6
	if (!m_perFrameCB.IsValid() || !m_perObjectCB.IsValid() || !m_lightCB.IsValid() || !m_postParamsCB.IsValid() ||
		!m_shadowPassCB.IsValid() || !m_shadowsCB.IsValid())
	{
		Log::Error("Renderer::Initialize : 상수버퍼 생성 실패.");
		return false;
	}

	if (!CreateSamplers() || !CreateBuiltinTextures() || !CreateShadowResources())
	{
		return false;
	}

	// ---- 바인딩 레이아웃. "이 파이프라인은 b0·b1·b2·b3·t0·t1·t8·t9·s0·s4·s5 를 쓴다"는 선언 ----
	BindingLayoutDesc frameLayout;
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
	// 다중 광원 그림자: b5 (그림자 패스가 뷰마다 올리는 행렬, VS), b6 (그림자 뷰 배열, PS).
	// 새 레이아웃이 아니라 프레임 레이아웃의 끝에 붙임 — D3D12 커맨드 리스트의 셋 슬롯(kMaxBindingSets = 4)이
	// 이미 가득 참.
	frameLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex, kCBSlotShadowPass);
	frameLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotShadows);
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotCsmMap);   // t14: CSM 전용 아틀라스
	m_frameLayout =m_device->CreateBindingLayout(frameLayout);

	BindingLayoutDesc objectLayout;
	objectLayout.debugName = "Layout_Object";
	objectLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex, kCBSlotPerObject);
	m_objectLayout = m_device->CreateBindingLayout(objectLayout);

	// 재질 셋: 상수(b3, uvScale 때문에 VS도), 알베도 t0, 노멀 t1, 금속성/거칠기 t2, AO t3, 발광 t4, 샘플러 s0.
	BindingLayoutDesc materialLayout;
	materialLayout.debugName = "Layout_Material";
	materialLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kCBSlotMaterial);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotAlbedo);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotNormal);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotMetallicRoughness);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotOcclusion);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotEmissive);
	materialLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotMaterial);
	m_materialLayout = m_device->CreateBindingLayout(materialLayout);

	// 후처리 셋: b0 (PerFrame: 노출·톤매핑·디버그 뷰), b4 (PostParams: 패스 파라미터), t13 (입력 텍스처), s6
	// (선형 Clamp). 슬롯 순서가 CreatePostSet 의 bindings 순서임. 모든 후처리 패스(톤매핑·블러·블룸)가 이
	// 레이아웃 하나를 씀 — 셰이더가 안 쓰는 슬롯도 항상 유효한 뷰가 꽂혀 있으므로 D3D12 디스크립터 테이블에도
	// 안전함.
	BindingLayoutDesc postLayout;
	postLayout.debugName = "Layout_Post";
	postLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotPerFrame);  // b0
	postLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotPostParams); // b4
	postLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotSceneHdr); // t13
	postLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotPost);        // s6
	m_postLayout = m_device->CreateBindingLayout(postLayout);


	bool layoutValid =
		(
			!m_frameLayout.IsValid() ||
			!m_objectLayout.IsValid() ||
			!m_materialLayout.IsValid() ||
			!m_postLayout.IsValid()
		);
	if (layoutValid)
	{
		Log::Error("Renderer::Initialize : BindingLayout 생성 실패.");
		return false;
	}

	// ---- 프레임/오브젝트 ResourceSet. 재질 셋은 재질마다 GetOrCreateGpuMaterial이 만듦 ----
	// 프레임 셋은 씬의 환경(스카이박스·IBL)이 바뀔 때 다시 만들어야 하므로 UpdateFrameSet 이 만듦. 처음에는
	// 빈 환경(검은 큐브·흰 LUT)을 꽂음.
	ResolveEnvironment(SceneEnvironment{});
	UpdateFrameSet();

	const ResourceSetDesc objectSet{
		.layout = m_objectLayout,
		.bindings = { { .buffer = m_perObjectCB } },
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

	// ---- PSO의 고정 부분. 나머지(깊이 테스트 켬, 불투명, 트라이앵글 리스트)는 Desc 기본값을 씀 ----
	// PipelineStateDesc 를 D3D12_GRAPHICS_PIPELINE_STATE_DESC 처럼 한 리터럴로 적음. 지정 초기화는 선언
	// 순서를 따라야 하므로 필드 순서는 PipelineTypes.h 와 같음: vs, ps, bindingLayouts, bindingLayoutCount,
	// vertexLayout, rasterizer, ..., rtvCount, dsvFormat.
	m_baseDesc = PipelineStateDesc
	{
		.vs = m_vs,
		.ps = m_ps,
		.bindingLayouts = { m_frameLayout, m_objectLayout, m_materialLayout },
		.bindingLayoutCount = 3,
	};

	// 그림자 패스: VS 만(ps 는 빈 핸들), 렌더 타깃 없음, 깊이 D32_FLOAT. 래스터라이저 깊이 바이어스로 자기
	// 그림자(acne)를 줄임. D32_FLOAT 에서 DepthBias 단위는 2^(지수−23) — z ≈ 0.5 면 한 단위가 6e-8 임.
	m_shadowDesc = PipelineStateDesc{
		.vs = m_shadowVs,
		.bindingLayouts = { m_frameLayout, m_objectLayout },
		.bindingLayoutCount = 2,
		.rasterizer = { .depthBias = 2000, .depthBiasClamp = 0.01f, .slopeScaledDepthBias = 2.0f },
		.rtvCount = 0,
		.dsvFormat = Format::D32_FLOAT,
	};

	// 스카이박스. 정점 입력이 없으므로 vertexLayout 은 기본값(속성 0개) 그대로 둠 — 입력 레이아웃 없이
	// SV_VertexID 만 씀. 깊이: 테스트는 켜고(LessEqual — VS 가 만든 깊이 1.0 이 클리어 값 1.0 과 같아도 통과)
	// 쓰기는 끔. 컬링: 전체 화면 삼각형 하나라 끔.
	m_skyDesc = PipelineStateDesc{
		.vs = m_skyVs,
		.ps = m_skyPs,
		.bindingLayouts = { m_frameLayout },
		.bindingLayoutCount = 1,
		.rasterizer = { .cull = CullMode::None },
		.depthStencil = { .depthEnable = true, .depthWrite = false, .depthFunc = CompareFunc::LessEqual },
	};

	m_tonemapDesc = PipelineStateDesc{
		.vs = m_screenVs,
		.ps = m_toneMapPs,
		.bindingLayouts = {m_postLayout},
		.bindingLayoutCount = 1,
		.rasterizer = {.cull = CullMode::None },					  // 컬링 되지는 않지만 혹시 모르니 꺼두기
		.depthStencil = {.depthEnable = false, .depthWrite = false }, // Tone에는 Depth가 없음
		.dsvFormat = Format::Unknown,								  // 깊이 없는 패스에 D24_UNORM_S8_UINT면 거부함.
	};

	// 블러·블룸. 톤 패스와 모양이 같으므로(풀스크린 삼각형, 깊이 없음, Layout_Post) 그 Desc 를 베이스로 ps 만 바꿈.
	// 가산 버전은 블룸 합성용: 출력 = 셰이더 출력 + 기존 값 (알파는 기존 값 유지).
	m_postDesc = m_tonemapDesc;
	m_postDesc.ps = m_postPs;
	m_postAddDesc = m_postDesc;
	m_postAddDesc.blend.rt[0] = RenderTargetBlendDesc{
		.blendEnable = true,
		.srcColor = BlendFactor::One, .dstColor = BlendFactor::One, .colorOp = BlendOp::Add,
		.srcAlpha = BlendFactor::Zero, .dstAlpha = BlendFactor::One, .alphaOp = BlendOp::Add,
	};

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

bool Renderer::CreateBuiltinTextures()
{
	m_whiteTexture = GetOrLoadTexture("builtin:white", false, nullptr);
	m_flatNormalTexture = GetOrLoadTexture("builtin:flatnormal", false, nullptr);
	m_blackCube = GetOrLoadCubemap("builtin:black");   // 환경이 없을 때 t9~t11 의 기본값
	return m_whiteTexture.IsValid() && m_flatNormalTexture.IsValid() && m_blackCube.IsValid();
}

void Renderer::ResolveEnvironment(const SceneEnvironment& environment)
{
	EnvironmentBinding binding;
	binding.skybox = binding.irradiance = binding.specular = m_blackCube;
	binding.brdfLut = m_whiteTexture;

	// 실패한 이름은 GetOrLoadCubemap 이 검은 큐브를 돌려주므로 "검은 큐브가 아님" = 로드 성공.
	auto load = [this](const std::string& name, uint32_t& mips) -> TextureHandle
	{
		if (name.empty()) return TextureHandle{};
		const TextureHandle handle = GetOrLoadCubemap(name, &mips);
		return handle != m_blackCube ? handle : TextureHandle{};
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
	else if (binding.specular != m_blackCube)
	{
		binding.irradiance = binding.specular;
		binding.irradianceLod = static_cast<float>(binding.specularMips >= 2 ? binding.specularMips - 2 : 0);
	}
	binding.hasEnvironment = irradiance.IsValid() || binding.specular != m_blackCube;

	// BRDF LUT 는 2D 텍스처 캐시를 씀 (선형, 부동소수점 DDS 면 HDR 경로). 실패하면 흰색이 돌아오므로 그것으로 판단함.
	if (!environment.brdfLut.empty())
	{
		const TextureHandle lut = GetOrLoadTexture(environment.brdfLut, false, nullptr);
		if (lut != m_whiteTexture) { binding.brdfLut = lut; binding.hasBrdfLut = true; }
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
	// bindings[i] 는 Initialize 의 frameLayout.Add(...) i 번째 슬롯과 짝임: b0, b2, t8, s4, t9, s5, t10, t11,
	// t12, b5, b6, t14. 환경 샘플러는 재질 프리셋의 LinearClamp 를 함께 씀 (큐브 샘플은 면 경계를 하드웨어가
	// 이어 주고, LUT 는 가장자리에서 반복되면 안 됨).
	const ResourceSetDesc frameSet{
		.layout = m_frameLayout,
		.bindings = {
			{ .buffer = m_perFrameCB }, { .buffer = m_lightCB }, { .texture = m_shadowMap }, { .sampler = m_shadowSampler },
			{ .texture = m_environment.skybox }, { .sampler = GetSampler(SamplerPreset::LinearClamp) },
			{ .texture = m_environment.irradiance }, { .texture = m_environment.specular }, { .texture = m_environment.brdfLut },
			{ .buffer = m_shadowPassCB }, { .buffer = m_shadowsCB }, { .texture = m_csmMap },
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

bool Renderer::CreateShadowResources()
{
	// 깊이 전용 텍스처지만 셰이더에서도 읽음 → Device 가 TYPELESS 리소스 + DSV(D32) + SRV(R32) 로 만듦.
	const TextureDesc desc{
		.width = kShadowAtlasSize,
		.height = kShadowAtlasSize,
		.format = Format::D32_FLOAT,
		.mipLevels = 1,
		.bindFlags = TextureBind_DepthStencil | TextureBind_ShaderResource,
		.debugName = "ShadowMap",
	};
	m_shadowMap = m_device->CreateTexture(desc);
	m_shadowMapState = ResourceState::Common;

	// 방향광 CSM 전용 아틀라스. 캐스케이드마다 해상도가 달라서 Texture2DArray 대신 한 장에 칸을 나눠 씀
	// (배치는 CsmRect).
	const TextureDesc csmDesc{
		.width = kCsmAtlasWidth,
		.height = kCsmAtlasHeight,
		.format = Format::D32_FLOAT,
		.mipLevels = 1,
		.bindFlags = TextureBind_DepthStencil | TextureBind_ShaderResource,
		.debugName = "CsmShadowMap",
	};
	m_csmMap = m_device->CreateTexture(csmDesc);
	m_csmMapState = ResourceState::Common;

	// 비교 샘플러: SampleCmp 가 "비교값(받는 쪽 깊이) ≤ 저장된 깊이" 면 1(빛 받음), 아니면 0 을 돌려주고
	// 이웃과 보간함(하드웨어 PCF). Border 1 은 아틀라스 전체 밖의 안전장치임. 각 뷰의 "뷰 밖 = 빛을 받음"은
	// PS 의 SampleShadowView 가 직접 판정하고, PCF 가 옆 타일을 읽지 않도록 타일 안쪽으로 clamp 함 (타일
	// 경계는 Border 가 아니라 이 clamp 가 막음).
	const SamplerDesc sampler{
		.filter = SamplerFilter::Comparison,
		.addressU = SamplerAddress::Border,
		.addressV = SamplerAddress::Border,
		.addressW = SamplerAddress::Border,
		.maxAnisotropy = 1,
		.compareFunc = CompareFunc::LessEqual,
		.borderColor = { 1.0f, 1.0f, 1.0f, 1.0f },
		.debugName = "Sampler_ShadowCompare",
	};
	m_shadowSampler = m_device->CreateSampler(sampler);

	if (!m_shadowMap.IsValid() || !m_csmMap.IsValid() || !m_shadowSampler.IsValid())
	{
		Log::Error("Renderer : 그림자 맵 리소스 생성 실패.");
		return false;
	}
	Log::Info("섀도 아틀라스: %ux%u D32_FLOAT (지역광 타일 %u × %u²), PCF 5x5", kShadowAtlasSize, kShadowAtlasSize,
		kShadowLocalTileCount, kShadowTileSize);
	Log::Info("CSM 아틀라스: %ux%u D32_FLOAT (캐스케이드 %u / %u / %u / %u)", kCsmAtlasWidth, kCsmAtlasHeight,
		kCsmCascadeSize[0], kCsmCascadeSize[1], kCsmCascadeSize[2], kCsmCascadeSize[3]);
	return true;
}

SamplerHandle Renderer::GetSampler(SamplerPreset preset) const
{
	const uint32_t index = static_cast<uint32_t>(preset);
	return index < kSamplerPresetCount ? m_samplers[index] : m_samplers[0];
}

TextureHandle Renderer::GetOrLoadTexture(const std::string& name, bool srgb, const Scene* scene)
{
	// 빈 이름 = 텍스처 없음. 캐시 항목을 따로 만들지 않고 기본 흰색을 그대로 씀.
	if (name.empty() && m_whiteTexture.IsValid())
	{
		return m_whiteTexture;
	}
	const std::string key = name + (srgb ? "|srgb" : "|linear");
	auto found = m_textureCache.find(key);
	if (found != m_textureCache.end())
	{
		return found->second;
	}

	TextureImage image;
	bool ok = false;
	std::string debugName = name;
	if (name.empty() || name == "builtin:white")
	{
		const uint8_t white[4] = { 255, 255, 255, 255 };
		ok = TextureLoader::CreateSolid(1, white, false, image);   // 흰색은 sRGB든 선형이든 1.0
		debugName = "builtin:white";
	}
	else if (name == "builtin:flatnormal")
	{
		// 탄젠트 공간 (0, 0, 1) = 평평한 노멀. 항상 선형.
		const uint8_t flat[4] = { 128, 128, 255, 255 };
		ok = TextureLoader::CreateSolid(1, flat, false, image);
	}
	else if (name == "builtin:gray128")
	{
		// 감마 검증용. sRGB 128 → 선형 0.216 → sRGB 뷰에 쓰면 다시 128.
		const uint8_t gray[4] = { 128, 128, 128, 255 };
		ok = TextureLoader::CreateSolid(4, gray, srgb, image);
	}
	else if (name == "builtin:checker")
	{
		const uint8_t a[4] = { 230, 230, 230, 255 };
		const uint8_t b[4] = { 40, 40, 48, 255 };
		ok = TextureLoader::CreateChecker(512, 16, a, b, srgb, true, image);
	}
	else if (const std::vector<uint8_t>* encoded = scene != nullptr ? scene->FindImage(name) : nullptr)
	{
		// 모델에서 가져온 이미지. 씬이 인코딩된 바이트를 들고 있고, 여기서 재질의 색공간에 맞춰 디코딩함.
		ok = TextureLoader::LoadFromMemory(encoded->data(), encoded->size(), srgb, true, image);
		if (ok) Log::Info("텍스처 디코딩: %s (%ux%u, 밉 %u, %s)", name.c_str(), image.desc.width, image.desc.height, image.desc.mipLevels, srgb ? "sRGB" : "linear");
	}
	else
	{
		// "asset:<Assets 기준 상대 경로>" 는 Textures\ 밖의 파일을 가리킴 (콘텐츠 브라우저에서 끌어다 놓은
		// Sponza 이미지 등).
		static const char* kAssetPrefix = "asset:";
		const std::wstring path = name.rfind(kAssetPrefix, 0) == 0
			? Paths::GetAssetPath(ToWide(name.substr(6)).c_str())
			: Paths::GetAssetPath((L"Textures\\" + ToWide(name)).c_str());
		ok = TextureLoader::LoadFromFile(path, srgb, true, image);
	}

	TextureHandle handle;
	if (ok)
	{
		image.desc.debugName = debugName.c_str();
		handle = m_device->CreateTexture(image.desc, image.subresources);   // C++20: vector → std::span (포인터+개수 대신)
	}
	if (!handle.IsValid())
	{
		Log::Warn("텍스처 '%s' 를 쓸 수 없어 흰색으로 대체.", name.c_str());
		handle = m_whiteTexture;
	}
	m_textureCache.emplace(key, handle);
	return handle;
}

TextureHandle Renderer::GetOrLoadCubemap(const std::string& name, uint32_t* mipLevels)
{
	auto found = m_cubeCache.find(name);
	if (found != m_cubeCache.end())
	{
		if (mipLevels != nullptr) *mipLevels = found->second.mipLevels;
		return found->second.handle;
	}

	// 8비트 큐브맵은 "색" 텍스처이므로 sRGB 로 읽음. 부동소수점(HDR) DDS 는 TextureLoader 가
	// R16G16B16A16_FLOAT 로 읽고 라벨을 무시함.
	TextureImage image;
	bool ok = false;
	if (name == "builtin:black")
	{
		ok = TextureLoader::CreateCube(1, [](const float[3], uint8_t rgba[4]) { rgba[0] = rgba[1] = rgba[2] = 0; rgba[3] = 255; }, false, false, image);
	}
	else if (name == "builtin:sky")
	{
		// 절차적 하늘: 방향의 높이(y)만 봄. 지평선(y = 0)의 옅은 하늘색에서 천정(y = 1)의 짙은 파랑으로,
		// 지평선 아래는 금방 지면색이 됨. 값은 sRGB 바이트 — 감마 공간에서 섞어도 눈에 띄는 차이는 없고, 밉
		// 생성만 선형에서 함(TextureLoader 의 TEX_FILTER_SRGB).
		ok = TextureLoader::CreateCube(128, [](const float d[3], uint8_t rgba[4])
		{
			static constexpr float zenith[3] = { 52.0f, 105.0f, 185.0f };
			static constexpr float horizon[3] = { 185.0f, 208.0f, 230.0f };
			static constexpr float ground[3] = { 92.0f, 88.0f, 82.0f };
			const float y = d[1];
			const float* target = y >= 0.0f ? zenith : ground;
			const float t = y >= 0.0f ? powf(y, 0.6f) : (std::min)(1.0f, -y * 4.0f);
			for (int i = 0; i < 3; ++i) rgba[i] = static_cast<uint8_t>(horizon[i] + (target[i] - horizon[i]) * t + 0.5f);
			rgba[3] = 255;
		}, true, true, image);
	}
	else if (name == "builtin:skyfaces")
	{
		// 방향 검증용: 주축(절댓값이 가장 큰 성분)으로 면을 고르고 면마다 색을 다르게 칠함 — +X 빨강, −X
		// 청록, +Y 초록, −Y 자홍, +Z 파랑, −Z 노랑 (마주 보는 면은 보색). 면을 4×4 칸으로 나누는 격자선이 면
		// 경계에서 끊김 없이 이어지면 TextureLoader 의 텍셀 → 방향 변환이 맞는 것.
		ok = TextureLoader::CreateCube(64, [](const float d[3], uint8_t rgba[4])
		{
			static constexpr uint8_t colors[6][3] = { { 220, 50, 50 }, { 50, 200, 200 }, { 60, 200, 60 }, { 200, 60, 200 }, { 60, 90, 230 }, { 230, 210, 50 } };
			const float ax = fabsf(d[0]), ay = fabsf(d[1]), az = fabsf(d[2]);
			int face;
			float u, v;   // 면 위의 나머지 두 성분 ÷ 주축 → [−1, 1]
			if (ax >= ay && ax >= az) { face = d[0] > 0.0f ? 0 : 1; u = d[1] / ax; v = d[2] / ax; }
			else if (ay >= az)        { face = d[1] > 0.0f ? 2 : 3; u = d[0] / ay; v = d[2] / ay; }
			else                      { face = d[2] > 0.0f ? 4 : 5; u = d[0] / az; v = d[1] / az; }
			const bool line = fabsf(u * 2.0f - roundf(u * 2.0f)) < 0.04f || fabsf(v * 2.0f - roundf(v * 2.0f)) < 0.04f;
			for (int i = 0; i < 3; ++i) rgba[i] = line ? static_cast<uint8_t>(colors[face][i] / 3) : colors[face][i];
			rgba[3] = 255;
		}, true, true, image);
	}
	else
	{
		// 2D 텍스처와 같은 경로 규칙: "asset:<Assets 기준>" 또는 Textures\ 기준. 파일이면 큐브 DDS, 폴더면 면
		// 이미지 6장.
		static const char* kAssetPrefix = "asset:";
		const std::wstring path = name.rfind(kAssetPrefix, 0) == 0
			? Paths::GetAssetPath(ToWide(name.substr(6)).c_str())
			: Paths::GetAssetPath((L"Textures\\" + ToWide(name)).c_str());
		ok = TextureLoader::LoadCubeFromFile(path, true, true, image);
	}

	CubeEntry entry;
	if (ok)
	{
		image.desc.debugName = name.c_str();
		entry.handle = m_device->CreateTexture(image.desc, image.subresources);
		entry.mipLevels = image.desc.mipLevels;
	}
	if (!entry.handle.IsValid())
	{
		Log::Warn("큐브맵 '%s' 를 쓸 수 없어 검은 큐브로 대체.", name.c_str());
		entry.handle = m_blackCube;   // 검은 큐브 자신을 만들다 실패했다면 빈 핸들 → Initialize 가 실패함
		entry.mipLevels = 1;
	}
	m_cubeCache.emplace(name, entry);
	if (mipLevels != nullptr) *mipLevels = entry.mipLevels;
	return entry.handle;
}

bool Renderer::LoadShaders(bool fromSourceOnly)
{
	std::vector<uint8_t> vsCode, psCode, shadowCode, skyVsCode, skyPsCode, screenVsCode, toneMapPsCode, postPsCode;
	std::wstring vsSource, psSource, shadowSource, skyVsSource, skyPsSource, screenVsSource, toneMapPsSource, postPsSource;

	if (fromSourceOnly)
	{
		vsSource = Paths::GetShaderSourcePath(kVertexShaderFile);
		psSource = Paths::GetShaderSourcePath(kPixelShaderFile);
		shadowSource = Paths::GetShaderSourcePath(kShadowShaderFile);
		skyVsSource = Paths::GetShaderSourcePath(kSkyVertexShaderFile);
		skyPsSource = Paths::GetShaderSourcePath(kSkyPixelShaderFile);
		screenVsSource = Paths::GetShaderSourcePath(kScreenVertexShaderFile);
		toneMapPsSource = Paths::GetShaderSourcePath(kTonemapPixelShaderFile);
		postPsSource = Paths::GetShaderSourcePath(kPostPixelShaderFile);
		

		if (!ShaderCompiler::CompileFromFile(vsSource, "VS_Main", ShaderStage::Vertex, vsCode)) return false;
		if (!ShaderCompiler::CompileFromFile(psSource, "PS_Main", ShaderStage::Pixel, psCode)) return false;
		if (!ShaderCompiler::CompileFromFile(shadowSource, "VS_Shadow", ShaderStage::Vertex, shadowCode)) return false;
		if (!ShaderCompiler::CompileFromFile(skyVsSource, "VS_Sky", ShaderStage::Vertex, skyVsCode)) return false;
		if (!ShaderCompiler::CompileFromFile(skyPsSource, "PS_Sky", ShaderStage::Pixel, skyPsCode)) return false;
		if (!ShaderCompiler::CompileFromFile(screenVsSource, "VS_Screen", ShaderStage::Vertex, screenVsCode)) return false;
		if (!ShaderCompiler::CompileFromFile(toneMapPsSource, "PS_Tonemap", ShaderStage::Pixel, toneMapPsCode)) return false;
		if (!ShaderCompiler::CompileFromFile(postPsSource, "PS_Post", ShaderStage::Pixel, postPsCode)) return false;
		
	}
	else
	{
		if (!ShaderCompiler::LoadOrCompile(kVertexShaderFile, "VS_Main", ShaderStage::Vertex, vsCode, &vsSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kPixelShaderFile, "PS_Main", ShaderStage::Pixel, psCode, &psSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kShadowShaderFile, "VS_Shadow", ShaderStage::Vertex, shadowCode, &shadowSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kSkyVertexShaderFile, "VS_Sky", ShaderStage::Vertex, skyVsCode, &skyVsSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kSkyPixelShaderFile, "PS_Sky", ShaderStage::Pixel, skyPsCode, &skyPsSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kScreenVertexShaderFile, "VS_Screen", ShaderStage::Vertex, screenVsCode, &screenVsSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kTonemapPixelShaderFile, "PS_Tonemap", ShaderStage::Pixel, toneMapPsCode, &toneMapPsSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kPostPixelShaderFile, "PS_Post", ShaderStage::Pixel, postPsCode, &postPsSource)) return false;
		
	}

#if defined(_DEBUG)
	// HLSL cbuffer 크기와 C++ 구조체 크기를 리플렉션으로 대조함. 어긋나면 실패.
	bool layoutOk = true;
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(vsCode, "PerFrame", sizeof(PerFrameConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(vsCode, "PerObject", sizeof(PerObjectConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(vsCode, "Material", sizeof(MaterialConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(psCode, "PerFrame", sizeof(PerFrameConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(psCode, "Lights", sizeof(LightConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(psCode, "Material", sizeof(MaterialConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(psCode, "Shadows", sizeof(ShadowConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(shadowCode, "ShadowPass", sizeof(ShadowPassConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(shadowCode, "PerObject", sizeof(PerObjectConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(skyVsCode, "PerFrame", sizeof(PerFrameConstants));   // view, proj
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(toneMapPsCode, "PerFrame", sizeof(PerFrameConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(toneMapPsCode, "PostParams", sizeof(PostParamsConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(postPsCode, "PostParams", sizeof(PostParamsConstants));

	if (!layoutOk)
	{
		Log::Error("Renderer : 상수버퍼 레이아웃 불일치.");
		return false;
	}
#endif

	// 셰이더가 실제로 쓰는 슬롯이 선언한 레이아웃 안에 있는지 검사함. Release에서는 경고만 함.
	if (!ValidateBindings(vsCode, psCode, shadowCode, skyVsCode, skyPsCode, screenVsCode, toneMapPsCode, postPsCode))
	{
#if defined(_DEBUG)
		return false;
#endif
	}

	auto makeShader = [this](ShaderStage stage, std::span<const uint8_t> code, const char* name)
	{
		// ShaderDesc::bytecode 는 std::span — vector 의 data()/size() 를 따로 옮기지 않음.
		const ShaderDesc desc{ .stage = stage, .bytecode = code, .debugName = name };
		return m_device->CreateShader(desc);
	};
	const ShaderHandle newVs = makeShader(ShaderStage::Vertex, vsCode, "BasicVS");
	const ShaderHandle newPs = makeShader(ShaderStage::Pixel, psCode, "BasicPS");
	const ShaderHandle newShadowVs = makeShader(ShaderStage::Vertex, shadowCode, "ShadowVS");
	const ShaderHandle newSkyVs = makeShader(ShaderStage::Vertex, skyVsCode, "SkyboxVS");
	const ShaderHandle newSkyPs = makeShader(ShaderStage::Pixel, skyPsCode, "SkyboxPS");
	const ShaderHandle newScreenVs = makeShader(ShaderStage::Vertex, screenVsCode, "ScreenVS");
	const ShaderHandle newToneMapPs = makeShader(ShaderStage::Pixel, toneMapPsCode, "ToneMapPS");
	const ShaderHandle newPostPs = makeShader(ShaderStage::Pixel, postPsCode, "PostPS");
	

	bool bShaderInValid = (!newVs.IsValid() ||
						!newPs.IsValid() ||
						!newShadowVs.IsValid() ||
						!newSkyVs.IsValid() ||
						!newSkyPs.IsValid() ||
						!newScreenVs.IsValid() ||
						!newToneMapPs.IsValid() ||
						!newPostPs.IsValid());

	if (bShaderInValid)
	{
		Log::Error("Renderer : 셰이더 생성 실패.");
		m_device->DestroyShader(newVs);
		m_device->DestroyShader(newPs);
		m_device->DestroyShader(newShadowVs);
		m_device->DestroyShader(newSkyVs);
		m_device->DestroyShader(newSkyPs);

		m_device->DestroyShader(newScreenVs);
		m_device->DestroyShader(newToneMapPs);
		m_device->DestroyShader(newPostPs);

		return false;
	}

	// 이전 셰이더는 PSO가 참조로 붙들고 있으므로 풀에서 지워도 캐시가 비워질 때까지 살아 있음.
	m_device->DestroyShader(m_vs);
	m_device->DestroyShader(m_ps);
	m_device->DestroyShader(m_shadowVs);
	m_device->DestroyShader(m_skyVs);
	m_device->DestroyShader(m_skyPs);
	m_device->DestroyShader(m_screenVs);
	m_device->DestroyShader(m_toneMapPs);
	m_device->DestroyShader(m_postPs);
	m_vs = newVs;
	m_ps = newPs;
	m_shadowVs = newShadowVs;
	m_skyVs = newSkyVs;
	m_skyPs = newSkyPs;
	m_screenVs = newScreenVs;
	m_toneMapPs = newToneMapPs;
	m_postPs = newPostPs;


	// 핫리로드 감시 목록: 소스 트리의 원본 여섯 개(VS·PS·ShadowVS·SkyboxVS·SkyboxPS·Common.hlsli).
	// Release에서도 목록은 만들지만 CheckHotReload가 동작하지 않음.
	m_watched.clear();
	for (const wchar_t* file :
		{
		kVertexShaderFile,
		kPixelShaderFile,
		kShadowShaderFile,
		kSkyVertexShaderFile,
		kSkyPixelShaderFile,
		kCommonShaderFile,
		kScreenVertexShaderFile,
		kTonemapPixelShaderFile,
		kPostPixelShaderFile
		}
		)
	{
		WatchedFile watched;
		watched.path = Paths::GetShaderSourcePath(file);
		watched.writeTime = ShaderCompiler::GetLastWriteTime(watched.path);
		m_watched.push_back(watched);
	}

	Log::Info("셰이더 로드: VS %s, PS %s, ShadowVS %s, SkyboxVS %s, SkyboxPS %s", Log::ToUtf8(vsSource.c_str()).c_str(), Log::ToUtf8(psSource.c_str()).c_str(),
		Log::ToUtf8(shadowSource.c_str()).c_str(), Log::ToUtf8(skyVsSource.c_str()).c_str(), Log::ToUtf8(skyPsSource.c_str()).c_str());
	return true;
}

bool Renderer::ValidateBindings(const std::vector<uint8_t>& vsCode, const std::vector<uint8_t>& psCode, const std::vector<uint8_t>& shadowVsCode,
	const std::vector<uint8_t>& skyVsCode, const std::vector<uint8_t>& skyPsCode,
	const std::vector<uint8_t>& screenVsCode, const std::vector<uint8_t>& toneMapPsCode, const std::vector<uint8_t>& postPsCode) const
{
	// 선언: 세 레이아웃의 슬롯 합집합. Renderer가 만든 Desc 내용을 그대로 다시 적음.
	// BindingType = 타입(b, t, s)
	// 레지스터 번호
	// 사용 가능한 스테이지(VS / PS)
	struct Declared { BindingType type; uint8_t reg; uint8_t stageMask; };

	const Declared declared[] =
	{
		{ BindingType::ConstantBuffer, kCBSlotPerFrame,     ShaderStageMask_Vertex | ShaderStageMask_Pixel },
		{ BindingType::ConstantBuffer, kCBSlotLights,       ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotShadowMap,   ShaderStageMask_Pixel },
		{ BindingType::Sampler,        kSamplerSlotShadow,  ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotEnvironment, ShaderStageMask_Pixel },
		{ BindingType::Sampler,        kSamplerSlotEnvironment, ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotIrradiance,  ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotPrefiltered, ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotBrdfLut,     ShaderStageMask_Pixel },
		{ BindingType::ConstantBuffer, kCBSlotShadowPass,   ShaderStageMask_Vertex },   // b5: 그림자 패스 행렬
		{ BindingType::ConstantBuffer, kCBSlotShadows,      ShaderStageMask_Pixel },    // b6: 그림자 뷰 배열
		{ BindingType::ShaderResource, kSRVSlotCsmMap,      ShaderStageMask_Pixel },    // t14: CSM 전용 아틀라스
		{ BindingType::ConstantBuffer, kCBSlotPerObject,    ShaderStageMask_Vertex },
		{ BindingType::ConstantBuffer, kCBSlotMaterial,     ShaderStageMask_Vertex | ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotAlbedo,      ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotNormal,      ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotMetallicRoughness, ShaderStageMask_Pixel },   // PBR
		{ BindingType::ShaderResource, kSRVSlotOcclusion,   ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotEmissive,    ShaderStageMask_Pixel },
		{ BindingType::Sampler,        kSamplerSlotMaterial, ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotSceneHdr, ShaderStageMask_Pixel },
		{ BindingType::ConstantBuffer, kCBSlotPostParams, ShaderStageMask_Pixel },
		{ BindingType::Sampler,        kSamplerSlotPost, ShaderStageMask_Pixel },
	};

	bool ok = true;
	auto check = [&](const std::vector<uint8_t>& code, uint8_t stage, const char* stageName)
	{
		std::vector<ShaderCompiler::ReflectedBinding> used;
		if (!ShaderCompiler::ReflectBindings(code, used)) return;   // 리플렉션이 불가능하면 검사 생략
		for (const auto& u : used)
		{
			bool found = false;
			for (const Declared& d : declared)
			{
				if (d.type == u.type && d.reg == u.reg && (d.stageMask & stage)) { found = true; break; }
			}
			const char* typeName = u.type == BindingType::ConstantBuffer ? "b" : (u.type == BindingType::ShaderResource ? "t" : "s");
			if (!found)
			{
				Log::Error("바인딩 대조: %s가 %s%u ('%s')를 쓰지만 BindingLayout에 그 스테이지로 선언되지 않음.",
					stageName, typeName, u.reg, u.name.c_str());
				ok = false;
			}
			else
			{
				Log::Info("바인딩 대조: %s %s%u ('%s') OK", stageName, typeName, u.reg, u.name.c_str());
			}
		}
	};
	check(vsCode, ShaderStageMask_Vertex, "VS");
	check(psCode, ShaderStageMask_Pixel, "PS");
	check(shadowVsCode, ShaderStageMask_Vertex, "ShadowVS");
	// 스카이박스 PSO 는 프레임 레이아웃만 씀. 위 목록은 세 레이아웃의 합집합이라 재질 슬롯을 써도 통과하지만,
	// 셰이더가 실제로 쓰는 것은 b0·t9·s5 뿐임.
	check(skyVsCode, ShaderStageMask_Vertex, "SkyboxVS");
	check(skyPsCode, ShaderStageMask_Pixel, "SkyboxPS");
	check(screenVsCode, ShaderStageMask_Vertex, "ScreenVS");
	check(toneMapPsCode, ShaderStageMask_Pixel, "ToneMapPS");
	check(postPsCode, ShaderStageMask_Pixel, "PostPS");
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

	// 새 셰이더 핸들로 PSO를 다시 만들어야 함. 캐시를 비우면 이전 세대 핸들은 무효가 되고,
	// 다음 프레임의 GetPipelineFor가 새 Desc(새 vs/ps 핸들)로 새 PSO를 만듦.
	m_baseDesc.vs = m_vs;
	m_baseDesc.ps = m_ps;
	m_shadowDesc.vs = m_shadowVs;
	m_skyDesc.vs = m_skyVs;
	m_skyDesc.ps = m_skyPs;
	m_tonemapDesc.vs = m_screenVs;
	m_tonemapDesc.ps = m_toneMapPs;
	m_postDesc.vs = m_postAddDesc.vs = m_screenVs;
	m_postDesc.ps = m_postAddDesc.ps = m_postPs;
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
	for (auto& entry : m_textureCache)
	{
		if (entry.second != m_whiteTexture) m_device->DestroyTexture(entry.second);
	}
	m_textureCache.clear();
	m_device->DestroyTexture(m_whiteTexture);
	m_whiteTexture = TextureHandle{};
	m_flatNormalTexture = TextureHandle{};
	// 큐브맵. 로드 실패 항목은 검은 큐브 핸들을 공유하므로 따로 한 번만 지움.
	for (auto& entry : m_cubeCache)
	{
		if (entry.second.handle != m_blackCube) m_device->DestroyTexture(entry.second.handle);
	}
	m_cubeCache.clear();
	m_device->DestroyTexture(m_blackCube);
	m_blackCube = TextureHandle{};
	m_environment = m_boundEnvironment = EnvironmentBinding{};
	SetSceneTarget(0, 0);
	DestroyHdrTargets();    // HDR
	DestroyStageThumbs();
	m_outputWidth = m_outputHeight = 0;
	SetPreviewTarget(0, 0);
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
	m_device->DestroyBindingLayout(m_frameLayout);
	m_device->DestroyBindingLayout(m_objectLayout);
	m_device->DestroyBindingLayout(m_materialLayout);
	m_device->DestroyBindingLayout(m_postLayout);
	m_device->DestroyBuffer(m_perFrameCB);
	m_device->DestroyBuffer(m_perObjectCB);
	m_device->DestroyBuffer(m_lightCB);
	m_device->DestroyBuffer(m_postParamsCB);
	m_device->DestroyBuffer(m_shadowPassCB);
	m_device->DestroyBuffer(m_shadowsCB);
	m_device->DestroyShader(m_vs);
	m_device->DestroyShader(m_ps);
	m_device->DestroyShader(m_shadowVs);
	m_device->DestroyShader(m_skyVs);
	m_device->DestroyShader(m_skyPs);
	m_device->DestroyShader(m_screenVs);
	m_device->DestroyShader(m_toneMapPs);
	m_device->DestroyShader(m_postPs);
	// PSO는 캐시가 소유함. 셰이더 핸들을 지워도 PSO는 자기 참조로 셰이더 객체를 붙들고 있음.

	m_perFrameCB = m_perObjectCB = m_lightCB = m_postParamsCB = m_shadowPassCB = m_shadowsCB = BufferHandle{};
	m_frameSet = m_objectSet = ResourceSetHandle{};
	m_frameLayout = m_objectLayout = m_materialLayout = m_postLayout = BindingLayoutHandle{};
	m_vs = m_ps = m_shadowVs = m_skyVs = m_skyPs = m_screenVs = m_toneMapPs = m_postPs = ShaderHandle{};
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
	if (releaseTextures)
	{
		// 내장 텍스처("builtin:")는 남김 — 흰색·평평한 노멀은 프레임/기본 재질이 계속 씀.
		for (auto it = m_textureCache.begin(); it != m_textureCache.end();)
		{
			if (it->first.rfind("builtin:", 0) == 0) { ++it; continue; }
			// 로드에 실패해 흰색으로 대체된 항목은 내장 핸들을 공유하므로 파괴하지 않음.
			if (it->second != m_whiteTexture && it->second != m_flatNormalTexture) m_device->DestroyTexture(it->second);
			it = m_textureCache.erase(it);
		}
		// 파일에서 읽은 큐브맵도 버림 (내장 큐브는 남김). 프레임 셋의 t9~t12 가 지운 텍스처를 가리킬 수 있음.
		for (auto it = m_cubeCache.begin(); it != m_cubeCache.end();)
		{
			if (it->first.rfind("builtin:", 0) == 0) { ++it; continue; }
			if (it->second.handle != m_blackCube) m_device->DestroyTexture(it->second.handle);
			it = m_cubeCache.erase(it);
		}
		// BRDF LUT 는 위의 2D 캐시 정리로 이미 파괴됐을 수 있음. 바인딩 기록을 비우면 다음 프레임에 셋을
		// 반드시 다시 만듦.
		m_boundEnvironment = EnvironmentBinding{};
	}
}

// ------------------------------------------------------------------ 프레임

XMMATRIX Renderer::ComputeLightViewProj(const LightData& light) const
{
	// 방향광은 위치가 없음. 씬 중심에서 빛의 반대 방향으로 distance 만큼 물러난 곳에 눈을 두고,
	// 직교 투영으로 orthoSize × orthoSize 영역을 봄. 씬 경계를 알게 되면 그 AABB 에 맞추는 것이 다음 단계임.
	XMVECTOR direction = XMLoadFloat3(&light.direction);
	if (XMVectorGetX(XMVector3LengthSq(direction)) < 0.000001f)
	{
		direction = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	}
	direction = XMVector3Normalize(direction);

	const XMVECTOR center = XMVectorSet(0.0f, 2.0f, 0.0f, 1.0f);
	const XMVECTOR eye = XMVectorSubtract(center, XMVectorScale(direction, m_shadowSettings.distance));
	// 빛이 거의 수직이면 up = +Y 가 시선과 평행해져 LookAt 이 깨짐. 그때는 +Z 를 up 으로 씀.
	const XMVECTOR up = (fabsf(XMVectorGetY(direction)) > 0.99f) ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	const XMMATRIX view = XMMatrixLookAtLH(eye, center, up);
	const float size = m_shadowSettings.orthoSize;
	const XMMATRIX proj = XMMatrixOrthographicLH(size, size, 0.1f, m_shadowSettings.distance * 2.0f + size);
	return view * proj;
}

XMMATRIX Renderer::ComputeSpotViewProj(const LightData& light, float& outTanHalfFov) const
{
	XMVECTOR direction = XMLoadFloat3(&light.direction);
	if (XMVectorGetX(XMVector3LengthSq(direction)) < 0.000001f)
	{
		direction = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	}
	direction = XMVector3Normalize(direction);

	const XMVECTOR eye = XMVectorSet(light.position.x, light.position.y, light.position.z, 1.0f);
	// ComputeLightViewProj 와 같은 규칙: 빛이 거의 수직이면 up = +Z
	const XMVECTOR up = (fabsf(XMVectorGetY(direction)) > 0.99f) ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	const XMMATRIX view = XMMatrixLookToLH(eye, direction, up);

	// 시야각 = 바깥 원뿔 전체 + 2° 여유. [5°, 120°] 로 clamp — 바깥 원뿔이 60° 를 넘으면 가장자리는 그림자가 없음.
	const float outerCos = std::clamp(light.outerConeCos, -1.0f, 1.0f);
	const float fov = std::clamp(2.0f * acosf(outerCos) + XMConvertToRadians(2.0f), XMConvertToRadians(5.0f), XMConvertToRadians(120.0f));
	const float nearZ = (light.range < 0.2f) ? light.range * 0.5f : 0.1f;
	const float farZ = (std::max)(light.range, nearZ + 0.01f);   // 괄호: Windows.h 의 max 매크로 회피
	outTanHalfFov = tanf(fov * 0.5f);
	return view * XMMatrixPerspectiveFovLH(fov, 1.0f, nearZ, farZ);
}

XMMATRIX Renderer::ComputePointFaceViewProj(const LightData& light, uint32_t face) const
{
	// 면 순서는 D3D 큐브맵과 같음 (+X, −X, +Y, −Y, +Z, −Z). PS 의 PointFace 가 같은 번호를 씀.
	static const XMFLOAT3 kLook[6] = { {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1} };
	static const XMFLOAT3 kUp[6] = { {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0} };
	face = (std::min)(face, 5u);

	const XMVECTOR eye = XMVectorSet(light.position.x, light.position.y, light.position.z, 1.0f);
	const XMMATRIX view = XMMatrixLookToLH(eye, XMLoadFloat3(&kLook[face]), XMLoadFloat3(&kUp[face]));

	// 시야각을 90° 보다 조금 넓힘: PCF 가 타일 가장자리 kShadowPcfBorder 텍셀 안쪽으로 clamp 되므로, 그
	// 안쪽이 정확히 90° 를 덮게 함.
	const float tanHalf = static_cast<float>(kShadowTileSize) / static_cast<float>(kShadowTileSize - 2 * kShadowPcfBorder);
	const float fov = 2.0f * atanf(tanHalf);
	return view * XMMatrixPerspectiveFovLH(fov, 1.0f, 0.05f, (std::max)(light.range, 0.06f));
}

uint32_t Renderer::AddShadowView(const XMMATRIX& viewProj, uint32_t x, uint32_t y, uint32_t size,
	const XMFLOAT4& params, uint32_t lightIndex, uint32_t atlas)
{
	static bool warnedFull = false;
	if (m_shadowViewCount >= MAX_SHADOW_VIEWS)
	{
		if (!warnedFull)
		{
			warnedFull = true;
			Log::Error("그림자 뷰가 가득 참 (최대 %u개). 이 뷰는 만들지 않음.", MAX_SHADOW_VIEWS);
		}
		return UINT32_MAX;
	}
	const uint32_t index = m_shadowViewCount++;
	ShadowView& view = m_shadowViews[index];
	XMStoreFloat4x4(&view.viewProj, viewProj);   // 행우선 그대로. 올릴 때 전치함
	view.x = x;
	view.y = y;
	view.size = size;
	view.atlas = atlas;
	view.params = params;
	view.cullSphere = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	view.lightIndex = lightIndex;
	return index;
}

uint32_t Renderer::AddCascadeViews(const Camera& camera, const LightData& light, uint32_t lightIndex)
{
	const uint32_t count = static_cast<uint32_t>(std::clamp(m_shadowSettings.cascadeCount, 2, 4));
	if (m_shadowViewCount + count > MAX_SHADOW_VIEWS) return UINT32_MAX;   // 캐스케이드는 번호가 연속이어야 하므로 통째로 거절

	// ---- 분할 (practical split): s[i] = λ · n · (f/n)^(i/N) + (1 − λ) · (n + (f − n) · i/N), s[0] = n, s[N] = f ----
	const float nearZ = camera.GetNearZ();
	const float farZ = (std::max)((std::min)(camera.GetFarZ(), m_shadowSettings.cascadeDistance), nearZ + 0.01f);
	const float lambda = std::clamp(m_shadowSettings.cascadeLambda, 0.0f, 1.0f);
	float split[5] = {};
	for (uint32_t i = 0; i <= count; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(count);
		split[i] = lambda * nearZ * powf(farZ / nearZ, t) + (1.0f - lambda) * (nearZ + (farZ - nearZ) * t);
	}

	// ---- 카메라와 광원 방향 ----
	const XMFLOAT3 cameraPosition = camera.GetPosition();
	const XMVECTOR eyePosition = XMLoadFloat3(&cameraPosition);
	const XMVECTOR forward = camera.GetForward();
	const XMVECTOR right = camera.GetRight();
	const XMVECTOR cameraUp = camera.GetUp();
	const float tanHalfFovY = tanf(camera.GetFovY() * 0.5f);
	const float aspect = camera.GetAspect();

	XMVECTOR direction = XMLoadFloat3(&light.direction);
	if (XMVectorGetX(XMVector3LengthSq(direction)) < 0.000001f) direction = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	direction = XMVector3Normalize(direction);
	// ComputeLightViewProj 와 같은 up 규칙
	const XMVECTOR lightUp = (fabsf(XMVectorGetY(direction)) > 0.99f) ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	const float back = m_shadowSettings.distance;   // 조각 뒤(광원 쪽)로 캐스터를 잡는 거리

	uint32_t first = UINT32_MAX;
	for (uint32_t c = 0; c < count; ++c)
	{
		// 1) 조각의 꼭짓점 8개: 앞면(split[c])과 뒷면(split[c+1])의 네 모서리
		XMVECTOR corners[8];
		for (uint32_t k = 0; k < 2; ++k)
		{
			const float d = split[c + k];
			const float h = d * tanHalfFovY;
			const float w = h * aspect;
			const XMVECTOR center = XMVectorAdd(eyePosition, XMVectorScale(forward, d));
			for (uint32_t m = 0; m < 4; ++m)
			{
				const float sx = (m & 1) ? 1.0f : -1.0f;
				const float sy = (m & 2) ? 1.0f : -1.0f;
				corners[k * 4 + m] = XMVectorAdd(center, XMVectorAdd(XMVectorScale(right, sx * w), XMVectorScale(cameraUp, sy * h)));
			}
		}

		for (uint32_t k = 0; k < 8; ++k) XMStoreFloat3(&m_cascadeCorners[c][k], corners[k]);   // 에디터 오버레이용

		// 2) 경계 구: 중심 = 꼭짓점 평균, 반지름 = 가장 먼 꼭짓점까지. 반지름을 1/16 단위로 올림 — 카메라가
		//    돌아도 투영 크기가 일정해 텍셀 스냅이 유효함.
		XMVECTOR sphereCenter = XMVectorZero();
		for (const XMVECTOR& corner : corners) sphereCenter = XMVectorAdd(sphereCenter, corner);
		sphereCenter = XMVectorScale(sphereCenter, 1.0f / 8.0f);
		float radius = 0.0f;
		for (const XMVECTOR& corner : corners) radius = (std::max)(radius, XMVectorGetX(XMVector3Length(XMVectorSubtract(corner, sphereCenter))));
		radius = ceilf(radius * 16.0f) / 16.0f;

		// 3) 광원 뷰: 눈 = 구 중심에서 빛의 반대로 (r + back). 직교 상자 한 변 2r, 깊이 0 .. 2r + back.
		const XMVECTOR lightEye = XMVectorSubtract(sphereCenter, XMVectorScale(direction, radius + back));
		const XMMATRIX view = XMMatrixLookToLH(lightEye, direction, lightUp);
		XMMATRIX proj = XMMatrixOrthographicLH(2.0f * radius, 2.0f * radius, 0.0f, 2.0f * radius + back);

		// 4) 텍셀 스냅: 월드 원점이 광원 클립에서 놓이는 위치를 텍셀 격자에 맞춤. 카메라가 움직여도 그림자
		//    텍셀이 월드에 붙어 있어 가장자리가 반짝이지 않음. 격자는 이 캐스케이드의 해상도(캐스케이드마다
		//    다름) 기준이어야 함.
		const AtlasRect rect = CsmRect(c);
		const float tile = static_cast<float>(rect.size);
		XMVECTOR origin = XMVector4Transform(XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), view * proj);
		origin = XMVectorScale(origin, tile * 0.5f);   // NDC [-1,1] → 텍셀 단위
		const XMVECTOR snapped = XMVectorRound(origin);
		const XMVECTOR offset = XMVectorScale(XMVectorSubtract(snapped, origin), 2.0f / tile);
		proj.r[3] = XMVectorAdd(proj.r[3], XMVectorSet(XMVectorGetX(offset), XMVectorGetY(offset), 0.0f, 0.0f));

		// 5) 사각형과 params. 바이어스는 단일 뷰와 같은 "월드 거리"가 되도록 깊이 범위 비로 환산함.
		const float singleRange = m_shadowSettings.distance * 2.0f + m_shadowSettings.orthoSize;
		const XMFLOAT4 params(m_shadowSettings.bias * singleRange / (2.0f * radius + back), 2.0f * radius / rect.size, 0.0f, 0.0f);
		const uint32_t index = AddShadowView(view * proj, rect.x, rect.y, rect.size, params, lightIndex, 1);   // 1 = CSM 아틀라스
		if (c == 0) first = index;
	}

	// PS 가 캐스케이드를 고를 때 쓰는 값. 카메라 기준으로 한 번만 만듦.
	m_cascadeCount = count;
	m_cascadeSplits = XMFLOAT4(split[1], count > 1 ? split[2] : 0.0f, count > 2 ? split[3] : 0.0f, count > 3 ? split[4] : 0.0f);
	m_cascadeOrigin = cameraPosition;
	XMStoreFloat3(&m_cascadeForward, forward);
	return first;
}

void Renderer::SetCascadeFreeze(bool freeze, const Camera& camera)
{
	m_cascadeFrozen = freeze;
	if (freeze) m_cascadeFreezeCamera = camera;
}

void Renderer::BuildShadowViews(const Camera& drawCamera, LightConstants& lights)
{
	// 고정 중이면 캐스케이드는 잡아 둔 사본 기준. 로컬 광원 등 나머지는 카메라를 쓰지 않음.
	const Camera& camera = m_cascadeFrozen ? m_cascadeFreezeCamera : drawCamera;
	m_shadowViewCount = 0;
	m_cascadeCount = 0;
	m_cascadeSplits = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
	m_skippedShadowLights = 0;
	m_usedLocalTiles = 0;
	for (uint32_t i = 0; i < MAX_LIGHTS; ++i) lights.lights[i].shadowIndex = -1;
	if (!m_settings.shadows) return;

	bool directionalDone = false;
	for (uint32_t i = 0; i < lights.lightCount; ++i)
	{
		LightData& light = lights.lights[i];
		if (light.castShadows == 0) continue;

		if (light.type == static_cast<uint32_t>(LightType::Directional))
		{
			// 방향광은 첫 번째 castShadows 방향광만. 두 번째부터는 자리를 주지 않음.
			if (directionalDone)
			{
				++m_skippedShadowLights;
				continue;
			}
			directionalDone = true;
			if (m_shadowSettings.cascadeEnabled && camera.IsUsePerspectiveProjection())
			{
				// CSM. 직교 카메라는 절두체 조각 계산이 맞지 않아 아래의 단일 뷰로 대신함.
				const uint32_t first = AddCascadeViews(camera, light, i);
				if (first != UINT32_MAX) light.shadowIndex = static_cast<int32_t>(first);
				else ++m_skippedShadowLights;
				continue;
			}
			// 단일 뷰도 CSM 전용 아틀라스(atlas 1)에 그림. 텍셀의 월드 크기는 그 뷰의 해상도 기준.
			const AtlasRect rect = CsmSingleRect();
			const XMFLOAT4 params(m_shadowSettings.bias, m_shadowSettings.orthoSize / rect.size, 0.0f, 0.0f);
			const uint32_t view = AddShadowView(ComputeLightViewProj(light), rect.x, rect.y, rect.size, params, i, 1);
			if (view != UINT32_MAX) light.shadowIndex = static_cast<int32_t>(view);
		}
		else if (light.type == static_cast<uint32_t>(LightType::Spot))
		{
			if (m_usedLocalTiles >= kShadowLocalTileCount)
			{
				++m_skippedShadowLights;
				continue;
			}
			float tanHalf = 1.0f;
			const XMMATRIX viewProj = ComputeSpotViewProj(light, tanHalf);
			const AtlasRect rect = LocalTileRect(m_usedLocalTiles++);
			// y = 거리 1 에서 텍셀 하나가 덮는 월드 길이 (2 · tan(fov/2) / 해상도). 셰이더가 거리를 곱함. z = 1 원근.
			const XMFLOAT4 params(0.0f, 2.0f * tanHalf / rect.size, 1.0f, 0.0f);
			const uint32_t view = AddShadowView(viewProj, rect.x, rect.y, rect.size, params, i);
			if (view != UINT32_MAX)
			{
				light.shadowIndex = static_cast<int32_t>(view);
				m_shadowViews[view].cullSphere = XMFLOAT4(light.position.x, light.position.y, light.position.z, light.range);
			}
		}
		else if (light.type == static_cast<uint32_t>(LightType::Point))
		{
			// 면 6개가 연속 타일이어야 함 (PS 가 shadowIndex + face 로 찾음). 6칸이 안 남으면 일부만 주지
			// 않고 통째로 건너뜀.
			if (m_usedLocalTiles + 6 > kShadowLocalTileCount || m_shadowViewCount + 6 > MAX_SHADOW_VIEWS)
			{
				++m_skippedShadowLights;
				continue;
			}
			const float tanHalf = static_cast<float>(kShadowTileSize) / static_cast<float>(kShadowTileSize - 2 * kShadowPcfBorder);
			const XMFLOAT4 params(0.0f, 2.0f * tanHalf / kShadowTileSize, 1.0f, 0.0f);
			uint32_t first = UINT32_MAX;
			for (uint32_t face = 0; face < 6; ++face)
			{
				const AtlasRect rect = LocalTileRect(m_usedLocalTiles++);
				const uint32_t view = AddShadowView(ComputePointFaceViewProj(light, face), rect.x, rect.y, rect.size, params, i);
				if (view == UINT32_MAX) break;   // 위에서 자리를 확인했으므로 일어나지 않음
				if (face == 0) first = view;
				m_shadowViews[view].cullSphere = XMFLOAT4(light.position.x, light.position.y, light.position.z, light.range);
			}
			if (first != UINT32_MAX) light.shadowIndex = static_cast<int32_t>(first);
		}
	}
}

void Renderer::UploadShadowConstants()
{
	// b6: 그림자 뷰 배열. 그림자가 꺼진 프레임(뷰 0개)에도 올림 — D3D12 는 Dynamic 상수버퍼가 프레임마다
	// 갱신되어야 경고하지 않음.
	ShadowConstants constants = {};
	for (uint32_t i = 0; i < m_shadowViewCount; ++i)
	{
		const ShadowView& view = m_shadowViews[i];
		ShadowViewData& data = constants.views[i];
		XMStoreFloat4x4(&data.viewProj, XMMatrixTranspose(XMLoadFloat4x4(&view.viewProj)));
		// 아틀라스마다 크기가 다름 (CSM 아틀라스는 가로·세로도 다름). UV 사각형은 그 뷰가 속한 아틀라스
		// 기준으로 환산함.
		const float width = static_cast<float>(view.atlas == 1 ? kCsmAtlasWidth : kShadowAtlasSize);
		const float height = static_cast<float>(view.atlas == 1 ? kCsmAtlasHeight : kShadowAtlasSize);
		data.atlasRect = XMFLOAT4(view.x / width, view.y / height, view.size / width, view.size / height);
		data.params = view.params;
		data.params.w = static_cast<float>(view.atlas);   // 셰이더가 t8 / t14 중 어느 쪽을 읽을지 고름
	}
	constants.atlasParams = XMFLOAT4(1.0f / kShadowAtlasSize, static_cast<float>(kShadowPcfBorder), m_shadowSettings.localNormalBias, 0.0f);
	constants.csmAtlasParams = XMFLOAT4(1.0f / kCsmAtlasWidth, 1.0f / kCsmAtlasHeight, 0.0f, 0.0f);
	// CSM 이 아니면 cascadeOrigin.w = 0 (PS 가 단일 뷰 경로를 탐)
	constants.cascadeSplits = m_cascadeSplits;
	constants.cascadeOrigin = XMFLOAT4(m_cascadeOrigin.x, m_cascadeOrigin.y, m_cascadeOrigin.z, static_cast<float>(m_cascadeCount));
	constants.cascadeForward = XMFLOAT4(m_cascadeForward.x, m_cascadeForward.y, m_cascadeForward.z, std::clamp(m_shadowSettings.cascadeBlend, 0.0f, 0.5f));   // w = 경계 블렌딩 비율
	m_device->UpdateBuffer(m_shadowsCB, &constants, sizeof(constants));

	// b5: 그림자 패스가 뷰마다 덮어쓸 자리. 여기서는 단위 행렬로 한 번 올려 둠 (그림자 패스가 없는 프레임의
	// 갱신 검사용).
	ShadowPassConstants pass = {};
	XMStoreFloat4x4(&pass.shadowViewProj, XMMatrixIdentity());
	m_device->UpdateBuffer(m_shadowPassCB, &pass, sizeof(pass));
}

void Renderer::Render(const Scene& scene, const Camera& camera, float totalTime)
{
	if (m_device == nullptr) return;

	CheckHotReload(totalTime);
	// HDR: 출력 크기(에디터는 씬 뷰, 게임은 백버퍼)에 맞는 HDR 텍스처가 있어야 그릴 수 있음. 만들지 못했으면
	// 이번 프레임은 건너뜀.
	EnsureHdrTargets();
	if (!m_hdrColor.IsValid() || !m_hdrPostSet.IsValid()) return;
	// 단계별 썸네일. 창이 요청한 프레임에만 (요청은 한 번 쓰면 지움 — 창을 닫으면 다음 프레임부터 멈춤).
	m_postCapture = m_postCaptureRequested && IsOffscreen();
	m_postCaptureRequested = false;
	for (PostStageThumb& thumb : m_stageThumbs) thumb.active = false;
	if (m_postCapture) EnsureStageThumbs(m_hdrWidth, m_hdrHeight);
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
	const std::vector<LightData>& sceneLights = scene.GetLights();

	// ---- b2 Lights 내용 (올리는 것은 아래에서). BuildShadowViews 가 lights.lights[i].shadowIndex 를 채우므로 b0·b6 보다 먼저 만듦 ----
	LightConstants lights = {};
	lights.lightCount = static_cast<uint32_t>(sceneLights.size() < MAX_LIGHTS ? sceneLights.size() : MAX_LIGHTS);
	lights.ambientColor = scene.ambientColor;
	for (uint32_t i = 0; i < lights.lightCount; ++i)
	{
		lights.lights[i] = sceneLights[i];

		// 셰이더는 정규화된 방향을 기대함. 0벡터면 아래를 향하게 함.
		const XMVECTOR direction = XMLoadFloat3(&lights.lights[i].direction);
		const float lengthSquared = XMVectorGetX(XMVector3LengthSq(direction));
		if (lengthSquared > 0.000001f)
		{
			XMStoreFloat3(&lights.lights[i].direction, XMVector3Normalize(direction));
		}
		else
		{
			lights.lights[i].direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
		}
	}

	// ---- 그림자 뷰: 조명마다 아틀라스 타일을 배정하고 shadowIndex 를 채움 → b6(+b5) → b0 (shadowParams.w 가 이 결과를 씀) ----
	BuildShadowViews(camera, lights);
	m_shadowEnabledThisFrame = m_shadowViewCount > 0;
	UploadShadowConstants();

	// ---- 환경 (스카이박스 t9, IBL t10~t12, 노출·톤매핑). b0 의 environmentParams·outputParams 가 이 값을 씀 ----
	ResolveEnvironment(scene.environment);

	// ---- b0 PerFrame ----
	UploadPerFrameConstants(camera, totalTime);

	// ---- b2 Lights ----
	m_device->UpdateBuffer(m_lightCB, &lights, sizeof(lights));

	BuildDrawList(scene);

	// 환경 텍스처를 프레임 셋 t9~t12 에 꽂음. 없는 것도 슬롯을 비우지 않고 검은 큐브·흰 LUT 를 꽂음
	// (D3D12 디스크립터 테이블에는 유효한 뷰가 있어야 함. 셰이더는 environmentParams·outputParams 의 플래그로
	// 쓸지 정함).
	UpdateFrameSet();

	// 프레임 셋(b0, b2, t8, s4, t9, s5, t10~t12)과 오브젝트 셋(b1)을 바인딩함. 그림자 패스는 t8(그림자 맵)을
	// DSV 로 쓰므로 BeginRenderPass 가 t8 SRV 바인딩을 해제함. 메인 패스에서 셋을 다시 바인딩해 t8 을 복구함.
	cmd.SetResourceSet(m_frameSet);
	cmd.SetResourceSet(m_objectSet);

	if (m_shadowEnabledThisFrame)
	{
		GpuProfileScope gpuScope(cmd, "ShadowPass");
		RenderShadowPass(cmd);
	}
	{
		GpuProfileScope gpuScope(cmd, "MainPass");
		RenderMainPass(cmd, scene);
	}
	// HDR 구간 후처리 (블러 → 블룸). 결과는 m_hdrColor 에 제자리로 남아 톤 패스가 그대로 읽음.
	ApplyHdrEffects(cmd, m_postMain, m_hdrColor, m_hdrColorState, m_hdrPostSet, m_hdrWidth, m_hdrHeight, m_postCapture);
	{
		// HDR: HDR 씬 → 노출·ACES·sRGB → 최종 출력. 에디터는 씬 뷰 텍스처, 게임은 백버퍼.
		// 미리보기 패스가 b0 를 다른 카메라로 덮어쓰기 전에 함 (톤 패스는 b0 의 출력 파라미터만 읽지만,
		// 카메라에 의존하지 않게 순서를 둠).
		GpuProfileScope gpuScope(cmd, "TonemapPass");
		if (IsOffscreen())
		{
			RenderTonemapPass(cmd, m_hdrPostSet, m_sceneColor, m_sceneColorState, ResourceState::ShaderResource, "TonemapPass",
					m_hdrWidth, m_hdrHeight, m_hdrWidth, m_hdrHeight);
			m_sceneColorState = ResourceState::ShaderResource;   // ImGui 가 샘플링함
		}
		else
		{
			// 백버퍼: Present → RenderTarget. UI 패스가 이어서 이 상태에서 그림 (BeginUIPass 는 전이하지 않음).
			RenderTonemapPass(cmd, m_hdrPostSet, m_device->GetBackBuffer(), ResourceState::Present, ResourceState::RenderTarget, "TonemapPass",
					m_hdrWidth, m_hdrHeight, m_hdrWidth, m_hdrHeight);
		}
	}
	if (m_previewCamera != nullptr && m_previewColor.IsValid() && m_previewHdr.IsValid())
	{
		// 선택한 카메라 오브젝트의 시점. b0 만 그 카메라로 바꿔 같은 드로우 목록을 작은 타깃에 다시 그림.
		GpuProfileScope gpuScope(cmd, "PreviewPass");
		UploadPerFrameConstants(*m_previewCamera, totalTime);
		RenderPreviewPass(cmd, scene);
		ApplyHdrEffects(cmd, m_postPreview, m_previewHdr, m_previewHdrState, m_previewHdrPostSet, m_previewWidth, m_previewHeight, false);
		RenderTonemapPass(cmd, m_previewHdrPostSet, m_previewColor, m_previewColorState, ResourceState::ShaderResource, "PreviewTonemapPass",
				m_previewWidth, m_previewHeight, m_previewWidth, m_previewHeight);
		m_previewColorState = ResourceState::ShaderResource;
	}

}

void Renderer::BuildDrawList(const Scene& scene)
{
	m_drawList.clear();
	for (const auto& objectPtr : scene.GetObjects())
	{
		const GameObject& object = *objectPtr;   // 씬이 unique_ptr 로 소유함
		const Mesh* mesh = object.GetMesh();
		if (mesh == nullptr || !mesh->IsValid()) continue;

		// 월드 행렬식이 음수(scale -1 미러)면 화면에서 삼각형 감기 순서가 뒤집혀 뒷면 컬링이 앞면을 자름.
		// 오브젝트마다 한 번만 판정.
		const bool mirrored = XMVectorGetX(XMMatrixDeterminant(object.GetWorldMatrix())) < 0.0f;

		// 서브메시마다 DrawItem 하나. 재질은 슬롯 → 오브젝트 기본 → Renderer 기본 순으로 고름.
		for (const Submesh& submesh : mesh->GetSubmeshes())
		{
			if (submesh.indexCount == 0) continue;
			const Material* material = object.GetMaterialForSlot(submesh.materialSlot);
			if (material == nullptr) material = &m_defaultMaterial;

			DrawItem item;
			item.mirrored = mirrored;
			item.pipeline = GetPipelineFor(*material, mesh->GetVertexFormat(), mirrored);
			item.material = material;
			item.mesh = mesh;
			item.object = &object;
			item.indexStart = submesh.indexStart;
			item.indexCount = submesh.indexCount;
			if (!item.pipeline.IsValid()) continue;
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

void Renderer::UploadObjectConstants(const GameObject& object)
{
	// b1 PerObject. 드로우마다 Map(WRITE_DISCARD). 드라이버가 매번 새 메모리를 주므로
	// 앞 드로우가 아직 실행되지 않았어도 그 데이터를 덮어쓰지 않음. 바인딩은 프레임 초에 한 번만 했음.
	const XMMATRIX world = object.GetWorldMatrix();   // 부모 계층을 곱한 월드 행렬 (Transform 의 것은 로컬)
	PerObjectConstants perObject = {};
	XMStoreFloat4x4(&perObject.world, XMMatrixTranspose(world));
	// 노멀 변환 행렬은 (W⁻¹)ᵀ. 전치해서 올려야 하므로 ((W⁻¹)ᵀ)ᵀ = W⁻¹ 를 그대로 저장함.
	XMStoreFloat4x4(&perObject.worldInvTranspose, XMMatrixInverse(nullptr, world));
	m_device->UpdateBuffer(m_perObjectCB, &perObject, sizeof(perObject));
}

void Renderer::RenderShadowPass(RHI::CommandList& cmd)
{
	m_stats.shadowViews = m_shadowViewCount;

	// 지역광 컬링용: 드로우 항목마다 월드 경계 구를 한 번만 계산함 (뷰마다, 아틀라스마다 다시 구하지 않음).
	// 중심 = 메시 경계 중심을 월드 행렬로 변환, 반지름 = 메시 경계 반지름 × 월드 행렬 세 축 길이의 최댓값
	// (비균등 스케일 대비).
	m_itemSpheres.resize(m_drawList.size());
	for (size_t k = 0; k < m_drawList.size(); ++k)
	{
		const DrawItem& item = m_drawList[k];
		const XMMATRIX world = item.object->GetWorldMatrix();
		const XMFLOAT3 localCenter = item.mesh->GetBounds().Center();
		const XMVECTOR center = XMVector3TransformCoord(XMLoadFloat3(&localCenter), world);
		const float scale = (std::max)({ XMVectorGetX(XMVector3Length(world.r[0])), XMVectorGetX(XMVector3Length(world.r[1])),
			XMVectorGetX(XMVector3Length(world.r[2])) });
		XMFLOAT3 c;
		XMStoreFloat3(&c, center);
		m_itemSpheres[k] = XMFLOAT4(c.x, c.y, c.z, item.mesh->GetBounds().Radius() * scale);
	}

	// 깊이 텍스처가 둘이라 패스도 둘: 0 = 메인 아틀라스(지역광·단일 방향광), 1 = CSM 전용 아틀라스.
	RenderShadowAtlasPass(cmd, 0);
	RenderShadowAtlasPass(cmd, 1);
}

void Renderer::RenderShadowAtlasPass(RHI::CommandList& cmd, uint32_t atlas)
{
	TextureHandle& map = (atlas == 1) ? m_csmMap : m_shadowMap;
	ResourceState& mapState = (atlas == 1) ? m_csmMapState : m_shadowMapState;

	// 이 아틀라스에 그릴 뷰가 없으면 패스를 열지 않음. 메인 패스가 t8 / t14 를 SRV 로 읽으므로 상태만
	// ShaderResource 로 맞춤.
	bool hasViews = false;
	for (uint32_t v = 0; v < m_shadowViewCount; ++v) hasViews = hasViews || (m_shadowViews[v].atlas == atlas);
	if (!hasViews)
	{
		if (mapState != ResourceState::ShaderResource)
		{
			cmd.Barrier(map, mapState, ResourceState::ShaderResource);
			mapState = ResourceState::ShaderResource;
		}
		return;
	}

	// 그림자 맵: ShaderResource(또는 Common) → DepthWrite. D3D11 에서는 상태 추적만 하고, D3D12 에서는 실제
	// 배리어를 기록함.
	cmd.Barrier(map, mapState, ResourceState::DepthWrite);
	mapState = ResourceState::DepthWrite;

	// RenderPassDesc 도 지정 초기화 — Vulkan 의 VkRenderingInfo 나 D3D12 의 BeginRenderPass 인자와 같은 모양이 됨.
	const RenderPassDesc pass{
		.colorCount = 0,
		.depth = { .texture = map, .load = LoadOp::Clear, .clearDepth = 1.0f },
		.debugName = (atlas == 1) ? "CsmShadowPass" : "ShadowPass",
	};
	cmd.BeginRenderPass(pass);   // 아틀라스 전체를 한 번 클리어. 뷰포트는 뷰마다 아래에서 바꿈

	PipelineHandle lastPipeline;
	const Mesh* lastMesh = nullptr;
	for (uint32_t v = 0; v < m_shadowViewCount; ++v)
	{
		const ShadowView& view = m_shadowViews[v];
		if (view.atlas != atlas) continue;

		// ① 이 뷰의 아틀라스 사각형으로 뷰포트(D3D12 는 시저도) 제한
		cmd.SetViewport(Viewport{ .x = static_cast<float>(view.x), .y = static_cast<float>(view.y),
			.width = static_cast<float>(view.size), .height = static_cast<float>(view.size) });
		// ② b5 에 이 뷰의 행렬 (전치). 드로우 직전에 D3D12 가 루트 CBV 를 다시 바인딩하므로 프레임 셋을 다시
		// 꽂을 필요 없음.
		ShadowPassConstants passConstants = {};
		XMStoreFloat4x4(&passConstants.shadowViewProj, XMMatrixTranspose(XMLoadFloat4x4(&view.viewProj)));
		m_device->UpdateBuffer(m_shadowPassCB, &passConstants, sizeof(passConstants));

		// ③ 드로우 루프
		for (size_t k = 0; k < m_drawList.size(); ++k)
		{
			const DrawItem& item = m_drawList[k];
			if (item.material->unlit) continue;   // 감마 검증 카드처럼 조명을 받지 않는 물체는 그림자도 만들지 않음

			// 지역광(cullSphere.w > 0): 광원 범위 구와 겹치지 않는 물체는 이 뷰에 그림자를 드리울 수 없음.
			// 방향광·캐스케이드는 w = 0 이라 컬링 안 함.
			if (view.cullSphere.w > 0.0f)
			{
				const XMFLOAT4& s = m_itemSpheres[k];
				const float dx = s.x - view.cullSphere.x, dy = s.y - view.cullSphere.y, dz = s.z - view.cullSphere.z;
				const float reach = s.w + view.cullSphere.w;
				if (dx * dx + dy * dy + dz * dz > reach * reach)
				{
					++m_stats.shadowCulled;
					continue;
				}
			}

			const PipelineHandle pipeline = GetShadowPipelineFor(*item.material, item.mesh->GetVertexFormat(), item.mirrored);
			if (!pipeline.IsValid()) continue;
			if (pipeline != lastPipeline)
			{
				cmd.SetPipelineState(pipeline);
				lastPipeline = pipeline;
				++m_stats.pipelineSwitches;
			}
			if (item.mesh != lastMesh)
			{
				const GpuMesh* gpuMesh = GetOrCreateGpuMesh(*item.mesh);
				if (gpuMesh == nullptr) continue;
				cmd.SetVertexBuffer(gpuMesh->vertexBuffer);
				cmd.SetIndexBuffer(gpuMesh->indexBuffer, gpuMesh->indexFormat);
				lastMesh = item.mesh;
				++m_stats.meshSwitches;
			}
			UploadObjectConstants(*item.object);
			cmd.DrawIndexed(item.indexCount, item.indexStart, 0);
			++m_stats.shadowDraws;
		}
	}

	cmd.EndRenderPass();

	// DepthWrite → ShaderResource. 메인 패스가 t8 / t14 로 읽음.
	cmd.Barrier(map, ResourceState::DepthWrite, ResourceState::ShaderResource);
	mapState = ResourceState::ShaderResource;
}

void Renderer::RenderMainPass(RHI::CommandList& cmd, const Scene& scene)
{
	// HDR: 씬은 선형 FP16 텍스처(m_hdrColor)에 그림. 최종 출력(씬 뷰 텍스처 또는 백버퍼)과 그 상태 전이는 톤
	// 패스가 맡음. 깊이는 출력과 같은 크기여야 하므로 예전처럼 에디터면 씬 뷰 깊이, 게임 모드면 장치 깊이
	// 버퍼를 씀.
	const TextureHandle depth = IsOffscreen() ? m_sceneDepth : m_device->GetDepthBuffer();

	cmd.Barrier(m_hdrColor, m_hdrColorState, ResourceState::RenderTarget);
	m_hdrColorState = ResourceState::RenderTarget;

	const RenderPassDesc pass{
		.colors = { {
			.texture = m_hdrColor,
			.load = LoadOp::Clear,
			.srgbView = false,   // FP16 에는 sRGB 뷰가 없음. 선형 값을 그대로 씀 — sRGB 인코딩은 톤 패스의 RTV 가 함
			.clearColor = { scene.clearColor[0], scene.clearColor[1], scene.clearColor[2], scene.clearColor[3] },   // 선형. 이제 노출·ACES 를 거침
		} },
		.colorCount = 1,
		.depth = { .texture = depth, .load = LoadOp::Clear },
		.debugName = "MainPass",
	};
	cmd.BeginRenderPass(pass);
	DrawItems(cmd, scene);
	cmd.EndRenderPass();

	// 톤 패스가 t13 으로 읽음.
	cmd.Barrier(m_hdrColor, ResourceState::RenderTarget, ResourceState::ShaderResource);
	m_hdrColorState = ResourceState::ShaderResource;
}

ResourceSetHandle Renderer::CreatePostSet(TextureHandle input, const char* debugName)
{
	// 슬롯 순서는 Initialize 의 postLayout.Add(...) 순서: b0 PerFrame, b4 PostParams, t13 입력, s6 선형 Clamp.
	const ResourceSetDesc desc{
		.layout = m_postLayout,
		.bindings = { { .buffer = m_perFrameCB }, { .buffer = m_postParamsCB }, { .texture = input }, { .sampler = GetSampler(SamplerPreset::LinearClamp) } },
		.debugName = debugName,
	};
	return m_device->CreateResourceSet(desc);
}

void Renderer::UploadPostParams(const PostParamsConstants& params)
{
	m_device->UpdateBuffer(m_postParamsCB, &params, sizeof(params));
}

void Renderer::RenderPostPass(RHI::CommandList& cmd, const PostPassDesc& desc)
{
	if (!desc.pipeline.IsValid() || !desc.input.IsValid() || !desc.output.IsValid()) return;

	// 출력: (에디터) 씬 뷰 텍스처 Common/ShaderResource → RenderTarget, (게임) 백버퍼 Present → RenderTarget.
	// D3D12 스왑체인 관례. D3D11 에서는 상태 추적만 함.
	cmd.Barrier(desc.output, desc.outputBefore, ResourceState::RenderTarget);

	// 전체 화면을 다 덮어쓰면 Load 도 Clear 도 필요 없음 (DontCare). 덧그리는 패스(블룸 합성)는 Load.
	// sRGB 뷰는 선형 → sRGB 인코딩을 함 (톤 패스만 씀. F5 로 끄면 UNORM 뷰).
	const RenderPassDesc pass{
		.colors = { { .texture = desc.output, .load = desc.load, .srgbView = desc.srgbView } },
		.colorCount = 1,
		.debugName = desc.debugName,
	};
	cmd.BeginRenderPass(pass);
	cmd.SetPipelineState(desc.pipeline);
	cmd.SetResourceSet(desc.input);   // b0 (PerFrame) + b4 (PostParams) + t13 (입력) + s6
	cmd.Draw(3, 0);                   // 정점 버퍼 없음 — ScreenVS 가 SV_VertexID 로 삼각형 하나를 만듦
	++m_stats.pipelineSwitches;
	++m_stats.draws;
	m_stats.triangles += 1;
	cmd.EndRenderPass();

	// ImGui 가 샘플링하거나 (씬 뷰·썸네일), 다음 후처리 패스가 읽거나, UI 패스가 이어서 백버퍼에 그림 (게임).
	cmd.Barrier(desc.output, ResourceState::RenderTarget, desc.outputAfter);
}

void Renderer::RenderTonemapPass(RHI::CommandList& cmd, ResourceSetHandle hdrSet, TextureHandle output,
	ResourceState outputBefore, ResourceState outputAfter, const char* debugName,
	uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH, bool skipGrade)
{
	PostParamsConstants params = {};
	params.view = XMFLOAT4(static_cast<float>(srcW), static_cast<float>(srcH), static_cast<float>(dstW), static_cast<float>(dstH));
	params.grade = XMFLOAT4(m_postSettings.greyscale, skipGrade ? 1.0f : 0.0f, 0.0f, 0.0f);
	UploadPostParams(params);   // 효과가 전부 꺼져도 매 프레임 올림 (D3D12 의 갱신 안 된 Dynamic CB 경고 방지)

	RenderPostPass(cmd, PostPassDesc{
		.pipeline = GetTonemapPipeline(),
		.input = hdrSet,
		.output = output,
		.outputBefore = outputBefore,
		.outputAfter = outputAfter,
		.load = LoadOp::DontCare,
		.srgbView = m_settings.srgbOutput,
		.debugName = debugName,
	});
}

void Renderer::RenderPreviewPass(RHI::CommandList& cmd, const Scene& scene)
{
	// HDR: 미리보기도 HDR 텍스처에 그리고, 톤 패스가 m_previewColor 로 옮김.
	cmd.Barrier(m_previewHdr, m_previewHdrState, ResourceState::RenderTarget);
	m_previewHdrState = ResourceState::RenderTarget;

	const RenderPassDesc pass{
		.colors = { {
			.texture = m_previewHdr,
			.load = LoadOp::Clear,
			.srgbView = false,
			.clearColor = { scene.clearColor[0], scene.clearColor[1], scene.clearColor[2], scene.clearColor[3] },
		} },
		.colorCount = 1,
		.depth = { .texture = m_previewDepth, .load = LoadOp::Clear },
		.debugName = "PreviewPass",
	};
	cmd.BeginRenderPass(pass);
	DrawItems(cmd, scene);
	cmd.EndRenderPass();

	cmd.Barrier(m_previewHdr, ResourceState::RenderTarget, ResourceState::ShaderResource);
	m_previewHdrState = ResourceState::ShaderResource;
}

void Renderer::DrawItems(RHI::CommandList& cmd, const Scene& scene)
{
	// 그림자 패스에서 t8 바인딩이 해제됐으므로 프레임 셋을 다시 바인딩함 (이제 그림자 맵은 SRV 상태).
	// 패스마다 새로 바인딩함.
	cmd.SetResourceSet(m_frameSet);
	cmd.SetResourceSet(m_objectSet);

	// ---- 드로우. "직전" 상태 기억은 이 패스 안에서만 유효함 ----
	PipelineHandle lastPipeline;
	const Material* lastMaterial = nullptr;
	const Mesh* lastMesh = nullptr;

	for (const DrawItem& item : m_drawList)
	{
		if (item.pipeline != lastPipeline)
		{
			cmd.SetPipelineState(item.pipeline);
			lastPipeline = item.pipeline;
			++m_stats.pipelineSwitches;
		}

		if (item.material != lastMaterial)
		{
			const GpuMaterial* gpuMaterial = GetOrCreateGpuMaterial(*item.material, &scene);
			if (gpuMaterial == nullptr) continue;
			cmd.SetResourceSet(gpuMaterial->set);
			lastMaterial = item.material;
			++m_stats.resourceSetSwitches;
		}

		if (item.mesh != lastMesh)
		{
			const GpuMesh* gpuMesh = GetOrCreateGpuMesh(*item.mesh);
			if (gpuMesh == nullptr) continue;
			cmd.SetVertexBuffer(gpuMesh->vertexBuffer);
			cmd.SetIndexBuffer(gpuMesh->indexBuffer, gpuMesh->indexFormat);
			lastMesh = item.mesh;
			++m_stats.meshSwitches;
		}

		// 서브메시 범위만 그림. 같은 메시의 다른 서브메시는 정렬 덕에 대개 바로 뒤에 옴 (VB/IB 재바인딩 없음).
		UploadObjectConstants(*item.object);
		cmd.DrawIndexed(item.indexCount, item.indexStart, 0);
		m_stats.triangles += item.indexCount / 3;
		++m_stats.draws;
	}

	// 불투명 물체를 다 그린 뒤 하늘. 깊이 버퍼가 채워져 있으므로 가려진 픽셀은 깊이 테스트에서 떨어짐.
	if (!scene.environment.skybox.empty())
	{
		DrawSkybox(cmd);
	}
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

void Renderer::SetPreviewTarget(uint32_t width, uint32_t height)
{
	if (m_device == nullptr) return;
	if (width == m_previewWidth && height == m_previewHeight) return;
	if (m_previewColor.IsValid()) m_device->DestroyTexture(m_previewColor);
	if (m_previewDepth.IsValid()) m_device->DestroyTexture(m_previewDepth);
	if (m_previewHdrPostSet.IsValid()) m_device->DestroyResourceSet(m_previewHdrPostSet);   // HDR
	if (m_previewHdr.IsValid()) m_device->DestroyTexture(m_previewHdr);
	m_previewColor = TextureHandle{};
	m_previewDepth = TextureHandle{};
	m_previewHdr = TextureHandle{};
	m_previewHdrPostSet = ResourceSetHandle{};
	DestroyPostScratch(m_postPreview);
	m_previewWidth = width;
	m_previewHeight = height;
	m_previewColorState = ResourceState::Common;
	m_previewHdrState = ResourceState::Common;
	if (width == 0 || height == 0) return;

	const TextureDesc color{
		.width = width,
		.height = height,
		.format = Format::R8G8B8A8_UNORM,   // 씬 뷰와 같은 TYPELESS 규칙 (sRGB RTV + UNORM SRV)
		.mipLevels = 1,
		.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource,
		.debugName = "CameraPreview_Color",
	};
	m_previewColor = m_device->CreateTexture(color);

	const TextureDesc depth{
		.width = width,
		.height = height,
		.format = Format::D24_UNORM_S8_UINT,
		.mipLevels = 1,
		.bindFlags = TextureBind_DepthStencil,
		.debugName = "CameraPreview_Depth",
	};
	m_previewDepth = m_device->CreateTexture(depth);

	// HDR: 미리보기 패스가 그리는 FP16 텍스처와, 톤 패스가 읽는 셋 (Layout_Post 순서: b0, t13).
	const TextureDesc hdr{
		.width = width,
		.height = height,
		.format = Format::R16G16B16A16_FLOAT,
		.mipLevels = 1,
		.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource,
		.debugName = "CameraPreview_HDR",
	};
	m_previewHdr = m_device->CreateTexture(hdr);
	if (m_previewHdr.IsValid())
	{
		m_previewHdrPostSet = CreatePostSet(m_previewHdr, "Set_Post_PreviewHDR");
	}

	if (!m_previewColor.IsValid() || !m_previewDepth.IsValid() || !m_previewHdr.IsValid() || !m_previewHdrPostSet.IsValid())
	{
		Log::Error("카메라 미리보기 텍스처 생성 실패 (%ux%u).", width, height);
		SetPreviewTarget(0, 0);
	}
}

// ------------------------------------------------------------------ 타깃

void Renderer::SetOutputSize(uint32_t width, uint32_t height)
{
	m_outputWidth = width;
	m_outputHeight = height;
}

void Renderer::DestroyHdrTargets()
{
	if (m_device == nullptr) return;
	// D3D12 는 지연 해제라 이미 기록된 명령이 읽어도 안전함 (SetSceneTarget 과 같은 이유).
	if (m_hdrPostSet.IsValid()) m_device->DestroyResourceSet(m_hdrPostSet);
	if (m_hdrColor.IsValid()) m_device->DestroyTexture(m_hdrColor);
	m_hdrPostSet = ResourceSetHandle{};
	m_hdrColor = TextureHandle{};
	DestroyPostScratch(m_postMain);   // 스크래치는 HDR 크기를 따름. 효과가 켜져 있으면 다음 프레임에 다시 만듦
	m_hdrWidth = 0;
	m_hdrHeight = 0;
	m_hdrColorState = ResourceState::Common;
}

void Renderer::EnsureHdrTargets()
{
	if (m_device == nullptr) return;

	// 목표 크기 = 최종 출력과 같은 크기. 에디터는 씬 뷰 텍스처, 게임 모드는 백버퍼 (깊이 버퍼도 같은 크기임).
	const uint32_t width = IsOffscreen() ? m_sceneWidth : m_outputWidth;
	const uint32_t height = IsOffscreen() ? m_sceneHeight : m_outputHeight;
	if (width == 0 || height == 0) return;
	if (m_hdrColor.IsValid() && m_hdrPostSet.IsValid() && width == m_hdrWidth && height == m_hdrHeight) return;

	DestroyHdrTargets();

	const TextureDesc desc{
		.width = width,
		.height = height,
		.format = Format::R16G16B16A16_FLOAT,   // 선형 half4. 1 을 넘는 값(태양, 강한 조명)을 잘라내지 않고 담음
		.mipLevels = 1,
		.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource,
		.debugName = "SceneHDR",
	};
	m_hdrColor = m_device->CreateTexture(desc);
	if (m_hdrColor.IsValid())
	{
		m_hdrPostSet = CreatePostSet(m_hdrColor, "Set_Post_HDR");
	}
	if (!m_hdrColor.IsValid() || !m_hdrPostSet.IsValid())
	{
		Log::Error("HDR 텍스처 생성 실패 (%ux%u).", width, height);
		DestroyHdrTargets();
		return;
	}
	m_hdrWidth = width;
	m_hdrHeight = height;
	Log::Info("HDR 텍스처 %ux%u (R16G16B16A16_FLOAT)", width, height);
}

void Renderer::BeginUIPass()
{
	if (m_device == nullptr) return;
	// ImGui 는 sRGB 로 인코딩된 색을 그대로 출력하므로 UNORM 뷰에 그림. 씬 위에 덧그리므로 Load, 깊이
	// 없음(depth 기본값 = 빈 핸들).
	RenderPassDesc pass{
		.colors = { { .texture = m_device->GetBackBuffer(), .load = LoadOp::Load, .srgbView = false } },
		.colorCount = 1,
		.debugName = "UIPass",
	};
	if (IsOffscreen())
	{
		// 씬이 백버퍼에 그려지지 않았으므로 여기서 전이하고 지움 (에디터 배경).
		m_device->GetCommandList().Barrier(m_device->GetBackBuffer(), ResourceState::Present, ResourceState::RenderTarget);
		pass.colors[0].load = LoadOp::Clear;
		pass.colors[0].clearColor[0] = 0.13f; pass.colors[0].clearColor[1] = 0.13f; pass.colors[0].clearColor[2] = 0.15f; pass.colors[0].clearColor[3] = 1.0f;
	}
	m_device->GetCommandList().BeginRenderPass(pass);
}

void Renderer::EndUIPass()
{
	if (m_device == nullptr) return;
	RHI::CommandList& cmd = m_device->GetCommandList();
	cmd.EndRenderPass();
	// RenderTarget → Present. Present 직전의 마지막 전이.
	cmd.Barrier(m_device->GetBackBuffer(), ResourceState::RenderTarget, ResourceState::Present);
	// UI 패스와 마지막 Present 전이까지 센 프레임 전체 값. 패널은 UI 패스 안에서 그려 이 값이 한 프레임 늦게 보임.
	m_stats.renderPasses = cmd.GetStats().renderPasses;
	m_stats.barriers = cmd.GetStats().barriers;
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

	const BufferDesc vbDesc{
		.size = static_cast<uint32_t>(sizeof(VERTEX) * mesh.GetVertices().size()),
		.usage = BufferUsage::Default,
		.bindFlags = BufferBind_Vertex,
		.stride = GetVertexStride(mesh.GetVertexFormat()),
		.debugName = "VB",
	};
	gpuMesh.vertexBuffer = m_device->CreateBuffer(vbDesc, mesh.GetVertices().data());

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

	// 재질이 가리키는 텍스처·샘플러가 바뀌었으면(ImGui 편집, 첫 생성) ResourceSet을 다시 만듦.
	// ResourceSet은 값 객체라 부분 갱신 기능이 없음 — D3D12 디스크립터 테이블도 다시 써야 함.
	const TextureHandle albedo = GetOrLoadTexture(material.albedoTexture, material.albedoSrgb, scene);
	const TextureHandle normal = material.normalTexture.empty() ? m_flatNormalTexture : GetOrLoadTexture(material.normalTexture, false, scene);
	// PBR: 빈 이름은 흰색 — 금속성/거칠기·AO·발광 모두 계수가 그대로 쓰임. 발광만 색이라 sRGB.
	const TextureHandle metallicRoughness = GetOrLoadTexture(material.metallicRoughnessTexture, false, scene);
	const TextureHandle occlusion = GetOrLoadTexture(material.occlusionTexture, false, scene);
	const TextureHandle emissive = GetOrLoadTexture(material.emissiveTexture, true, scene);
	const SamplerPreset preset = (m_settings.samplerOverride >= 0 && m_settings.samplerOverride < static_cast<int>(kSamplerPresetCount))
		? static_cast<SamplerPreset>(m_settings.samplerOverride) : material.sampler;
	const SamplerHandle sampler = GetSampler(preset);
	if (!gpuMaterial->set.IsValid() || gpuMaterial->boundAlbedo != albedo || gpuMaterial->boundNormal != normal || gpuMaterial->boundSampler != sampler
		|| gpuMaterial->boundMetallicRoughness != metallicRoughness || gpuMaterial->boundOcclusion != occlusion || gpuMaterial->boundEmissive != emissive)
	{
		m_device->DestroyResourceSet(gpuMaterial->set);
		// 슬롯 순서는 Initialize 의 materialLayout.Add(...) 순서: b3 상수, t0 알베도, t1 노멀, t2
		// 금속성/거칠기, t3 AO, t4 발광, s0 샘플러.
		const ResourceSetDesc setDesc{
			.layout = m_materialLayout,
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

	// 재질 상수는 프레임에 한 번 올림. 편집(ImGui)이 바로 반영되도록 매 프레임 올리되,
	// 같은 재질을 여러 오브젝트가 쓰면 그중 첫 드로우에서만 올림.
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

PipelineHandle Renderer::GetPipelineFor(const Material& material, VertexFormat vertexFormat, bool mirrored)
{
	PipelineStateDesc desc = m_baseDesc;
	desc.vertexLayout = GetVertexLayout(vertexFormat);
	desc.rasterizer.frontCounterClockwise = mirrored ? !desc.rasterizer.frontCounterClockwise : desc.rasterizer.frontCounterClockwise;
	desc.rasterizer.fill = (m_settings.wireframe || material.wireframe) ? FillMode::Wireframe : FillMode::Solid;
	desc.rasterizer.cull = (m_settings.cullBack && !material.doubleSided) ? CullMode::Back : CullMode::None;
	// D3D12 PSO 는 렌더 타깃 뷰의 포맷을 알아야 함. 메인 패스는 FP16 HDR 텍스처에 그리므로 항상 R16G16B16A16_FLOAT.
	// (sRGB 여부는 톤 패스의 PSO 만 따짐 — GetTonemapPipeline)
	desc.rtvFormats[0] = Format::R16G16B16A16_FLOAT;
	desc.rtvCount = 1;
	desc.dsvFormat = Format::D24_UNORM_S8_UINT;
	return m_device->CreatePipeline(desc);
}

PipelineHandle Renderer::GetShadowPipelineFor(const Material& material, VertexFormat vertexFormat, bool mirrored)
{
	// 그림자는 항상 솔리드로 그림 (와이어프레임 재질도 실체는 있음). 양면 재질은 컬링 없이 그림.
	PipelineStateDesc desc = m_shadowDesc;
	desc.vertexLayout = GetVertexLayout(vertexFormat);
	desc.rasterizer.frontCounterClockwise = mirrored ? !desc.rasterizer.frontCounterClockwise : desc.rasterizer.frontCounterClockwise;
	desc.rasterizer.cull = material.doubleSided ? CullMode::None : CullMode::Back;
	return m_device->CreatePipeline(desc);
}

PipelineHandle Renderer::GetSkyPipeline()
{
	// 메인 패스와 같은 타깃에 그리므로 RTV/DSV 포맷도 메인 PSO 와 같게 맞춤 (D3D12 PSO 는 이 값이 다르면 그릴 수 없음).
	// 와이어프레임 설정은 따르지 않음 — 화면을 덮는 삼각형 하나의 모서리만 보여 의미가 없음.
	PipelineStateDesc desc = m_skyDesc;
	desc.rtvFormats[0] = Format::R16G16B16A16_FLOAT;   // HDR: 메인 PSO 와 같은 FP16 타깃
	desc.rtvCount = 1;
	desc.dsvFormat = Format::D24_UNORM_S8_UINT;
	return m_device->CreatePipeline(desc);
}

PipelineHandle Renderer::GetTonemapPipeline()
{
	// 톤 패스는 최종 출력 타깃(에디터는 씬 뷰 텍스처, 게임은 백버퍼)에 그림. RTV 포맷은 그 타깃의 뷰와 같아야
	// 함 (D3D12 PSO). 깊이 없음 — dsvFormat 은 m_tonemapDesc 의 Unknown 그대로 둠.
	PipelineStateDesc desc = m_tonemapDesc;
	desc.rtvFormats[0] = m_settings.srgbOutput ? Format::R8G8B8A8_UNORM_SRGB : Format::R8G8B8A8_UNORM;
	desc.rtvCount = 1;
	return m_device->CreatePipeline(desc);
}

// ------------------------------------------------------------------ 오프스크린 씬 뷰

void Renderer::SetSceneTarget(uint32_t width, uint32_t height)
{
	if (m_device == nullptr) return;
	if (width == m_sceneWidth && height == m_sceneHeight) return;

	// 이전 텍스처는 Destroy 함 — D3D12 는 지연 해제(실행 중인 프레임의 ImGui 드로우가 아직 읽을 수 있음),
	// D3D11 은 참조 카운트로 처리.
	if (m_sceneColor.IsValid()) m_device->DestroyTexture(m_sceneColor);
	if (m_sceneDepth.IsValid()) m_device->DestroyTexture(m_sceneDepth);
	m_sceneColor = TextureHandle{};
	m_sceneDepth = TextureHandle{};
	m_sceneWidth = width;
	m_sceneHeight = height;
	m_sceneColorState = ResourceState::Common;
	if (width == 0 || height == 0) return;

	const TextureDesc color{
		.width = width,
		.height = height,
		.format = Format::R8G8B8A8_UNORM,   // TYPELESS 리소스: sRGB RTV + UNORM SRV
		.mipLevels = 1,
		.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource,
		.debugName = "SceneView_Color",
	};
	m_sceneColor = m_device->CreateTexture(color);

	const TextureDesc depth{
		.width = width,
		.height = height,
		.format = Format::D24_UNORM_S8_UINT,
		.mipLevels = 1,
		.bindFlags = TextureBind_DepthStencil,
		.debugName = "SceneView_Depth",
	};
	m_sceneDepth = m_device->CreateTexture(depth);

	if (!m_sceneColor.IsValid() || !m_sceneDepth.IsValid())
	{
		Log::Error("씬 뷰 텍스처 생성 실패 (%ux%u).", width, height);
		SetSceneTarget(0, 0);
		return;
	}
	Log::Info("씬 뷰 텍스처 %ux%u", width, height);
}

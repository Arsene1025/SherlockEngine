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
#include "Core/Profiler.h"   // 10단계: GPU 구간
#include <algorithm>
#include <cmath>

using namespace DirectX;   // 이 파일 안에서만

namespace
{
	constexpr const wchar_t* kVertexShaderFile = L"BasicVertexShader.hlsl";
	constexpr const wchar_t* kPixelShaderFile = L"BasicPixelShader.hlsl";
	constexpr const wchar_t* kShadowShaderFile = L"ShadowVertexShader.hlsl";
	constexpr const wchar_t* kCommonShaderFile = L"Common.hlsli";
	constexpr float kHotReloadPollInterval = 0.5f;   // 초

	std::wstring ToWide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
		return wide;
	}
}

Renderer::Renderer()
{
	m_defaultMaterial.name = "Default";
	XMStoreFloat4x4(&m_lightViewProj, XMMatrixIdentity());
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
	auto makeCB = [this](uint32_t size, const char* name)
	{
		BufferDesc desc;
		desc.size = size;
		desc.usage = BufferUsage::Dynamic;
		desc.bindFlags = BufferBind_Constant;
		desc.debugName = name;
		return m_device->CreateBuffer(desc);
	};
	m_perFrameCB = makeCB(sizeof(PerFrameConstants), "CB_PerFrame");
	m_perObjectCB = makeCB(sizeof(PerObjectConstants), "CB_PerObject");
	m_lightCB = makeCB(sizeof(LightConstants), "CB_Lights");
	if (!m_perFrameCB.IsValid() || !m_perObjectCB.IsValid() || !m_lightCB.IsValid())
	{
		Log::Error("Renderer::Initialize : 상수버퍼 생성 실패.");
		return false;
	}

	if (!CreateSamplers() || !CreateBuiltinTextures() || !CreateShadowResources())
	{
		return false;
	}

	// ---- 바인딩 레이아웃. "이 파이프라인은 b0·b1·b2·b3·t0·t1·t8·s0·s4 를 쓴다"의 선언 ----
	BindingLayoutDesc frameLayout;
	frameLayout.debugName = "Layout_Frame";
	frameLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kCBSlotPerFrame);
	frameLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Pixel, kCBSlotLights);
	frameLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotShadowMap);
	frameLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotShadow);
	m_frameLayout = m_device->CreateBindingLayout(frameLayout);

	BindingLayoutDesc objectLayout;
	objectLayout.debugName = "Layout_Object";
	objectLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex, kCBSlotPerObject);
	m_objectLayout = m_device->CreateBindingLayout(objectLayout);

	// 재질 셋: 상수(b3, uvScale 때문에 VS도), 알베도 t0, 노멀 t1, 샘플러 s0.
	BindingLayoutDesc materialLayout;
	materialLayout.debugName = "Layout_Material";
	materialLayout.Add(BindingType::ConstantBuffer, ShaderStageMask_Vertex | ShaderStageMask_Pixel, kCBSlotMaterial);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotAlbedo);
	materialLayout.Add(BindingType::ShaderResource, ShaderStageMask_Pixel, kSRVSlotNormal);
	materialLayout.Add(BindingType::Sampler, ShaderStageMask_Pixel, kSamplerSlotMaterial);
	m_materialLayout = m_device->CreateBindingLayout(materialLayout);

	if (!m_frameLayout.IsValid() || !m_objectLayout.IsValid() || !m_materialLayout.IsValid())
	{
		Log::Error("Renderer::Initialize : BindingLayout 생성 실패.");
		return false;
	}

	// ---- 프레임/오브젝트 ResourceSet. 재질 셋은 재질마다 GetOrCreateGpuMaterial이 만든다 ----
	ResourceSetDesc frameSet;
	frameSet.layout = m_frameLayout;
	frameSet.bindings[0].buffer = m_perFrameCB;
	frameSet.bindings[1].buffer = m_lightCB;
	frameSet.bindings[2].texture = m_shadowMap;
	frameSet.bindings[3].sampler = m_shadowSampler;
	frameSet.debugName = "Set_Frame";
	m_frameSet = m_device->CreateResourceSet(frameSet);

	ResourceSetDesc objectSet;
	objectSet.layout = m_objectLayout;
	objectSet.bindings[0].buffer = m_perObjectCB;
	objectSet.debugName = "Set_Object";
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

	// ---- PSO의 고정 부분. 나머지(깊이 테스트 켬, 불투명, 트라이앵글 리스트)는 Desc 기본값 ----
	m_baseDesc = PipelineStateDesc{};
	m_baseDesc.vs = m_vs;
	m_baseDesc.ps = m_ps;
	m_baseDesc.bindingLayouts[0] = m_frameLayout;
	m_baseDesc.bindingLayouts[1] = m_objectLayout;
	m_baseDesc.bindingLayouts[2] = m_materialLayout;
	m_baseDesc.bindingLayoutCount = 3;

	// 그림자 패스: VS 만, 렌더 타깃 없음, 깊이 D32_FLOAT. 래스터라이저 깊이 바이어스로 자기 그림자(acne)를 줄인다.
	// D32_FLOAT 에서 DepthBias 단위는 2^(지수−23) — z ≈ 0.5 면 한 단위가 6e-8 이다.
	m_shadowDesc = PipelineStateDesc{};
	m_shadowDesc.vs = m_shadowVs;
	m_shadowDesc.ps = ShaderHandle{};
	m_shadowDesc.bindingLayouts[0] = m_frameLayout;
	m_shadowDesc.bindingLayouts[1] = m_objectLayout;
	m_shadowDesc.bindingLayoutCount = 2;
	m_shadowDesc.rtvCount = 0;
	m_shadowDesc.dsvFormat = Format::D32_FLOAT;
	m_shadowDesc.rasterizer.depthBias = 2000;
	m_shadowDesc.rasterizer.slopeScaledDepthBias = 2.0f;
	m_shadowDesc.rasterizer.depthBiasClamp = 0.01f;

	// 첫 프레임 전에 기본 PSO를 만들어 두어 생성 실패를 초기화 단계에서 잡는다.
	if (!GetPipelineFor(m_defaultMaterial, VertexFormat::PositionColorNormalTexcoordTangent).IsValid() ||
		!GetShadowPipelineFor(m_defaultMaterial, VertexFormat::PositionColorNormalTexcoordTangent).IsValid())
	{
		Log::Error("Renderer::Initialize : 기본 PSO 생성 실패.");
		return false;
	}

	return true;
}

bool Renderer::CreateSamplers()
{
	// SamplerPreset 순서와 같아야 한다.
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
		SamplerDesc desc;
		desc.filter = presets[i].filter;
		desc.addressU = desc.addressV = desc.addressW = presets[i].address;
		desc.maxAnisotropy = 16;
		desc.maxLod = presets[i].maxLod;
		desc.debugName = presets[i].name;
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
	return m_whiteTexture.IsValid() && m_flatNormalTexture.IsValid();
}

bool Renderer::CreateShadowResources()
{
	// 깊이 전용 텍스처인데 셰이더가 읽는다 → Device 가 TYPELESS 리소스 + DSV(D32) + SRV(R32) 로 만든다.
	TextureDesc desc;
	desc.width = kShadowMapSize;
	desc.height = kShadowMapSize;
	desc.format = Format::D32_FLOAT;
	desc.mipLevels = 1;
	desc.bindFlags = TextureBind_DepthStencil | TextureBind_ShaderResource;
	desc.debugName = "ShadowMap";
	m_shadowMap = m_device->CreateTexture(desc);
	m_shadowMapState = ResourceState::Common;

	// 비교 샘플러: SampleCmp 가 "저장된 깊이 ≤ 비교값" 을 0/1 로 돌려주고 이웃과 보간한다(하드웨어 PCF).
	// 맵 밖은 Border 1 = 빛을 받는 것으로.
	SamplerDesc sampler;
	sampler.filter = SamplerFilter::Comparison;
	sampler.addressU = sampler.addressV = sampler.addressW = SamplerAddress::Border;
	sampler.compareFunc = CompareFunc::LessEqual;
	sampler.maxAnisotropy = 1;
	sampler.borderColor[0] = sampler.borderColor[1] = sampler.borderColor[2] = sampler.borderColor[3] = 1.0f;
	sampler.debugName = "Sampler_ShadowCompare";
	m_shadowSampler = m_device->CreateSampler(sampler);

	if (!m_shadowMap.IsValid() || !m_shadowSampler.IsValid())
	{
		Log::Error("Renderer : 그림자 맵 리소스 생성 실패.");
		return false;
	}
	Log::Info("그림자 맵: %ux%u D32_FLOAT (DSV + SRV), PCF 3x3", kShadowMapSize, kShadowMapSize);
	return true;
}

SamplerHandle Renderer::GetSampler(SamplerPreset preset) const
{
	const uint32_t index = static_cast<uint32_t>(preset);
	return index < kSamplerPresetCount ? m_samplers[index] : m_samplers[0];
}

TextureHandle Renderer::GetOrLoadTexture(const std::string& name, bool srgb, const Scene* scene)
{
	// 빈 이름 = 텍스처 없음. 캐시 항목을 따로 만들지 않고 기본 흰색을 그대로 쓴다.
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
		// 9단계: 모델이 가져온 이미지. 씬이 인코딩된 바이트를 들고 있고 여기서 재질의 색공간으로 디코딩한다.
		ok = TextureLoader::LoadFromMemory(encoded->data(), encoded->size(), srgb, true, image);
		if (ok) Log::Info("텍스처 디코딩: %s (%ux%u, 밉 %u, %s)", name.c_str(), image.desc.width, image.desc.height, image.desc.mipLevels, srgb ? "sRGB" : "linear");
	}
	else
	{
		const std::wstring path = Paths::GetAssetPath((L"Textures\\" + ToWide(name)).c_str());
		ok = TextureLoader::LoadFromFile(path, srgb, true, image);
	}

	TextureHandle handle;
	if (ok)
	{
		image.desc.debugName = debugName.c_str();
		handle = m_device->CreateTexture(image.desc, image.subresources.data(), static_cast<uint32_t>(image.subresources.size()));
	}
	if (!handle.IsValid())
	{
		Log::Warn("텍스처 '%s' 를 쓸 수 없어 흰색으로 대체.", name.c_str());
		handle = m_whiteTexture;
	}
	m_textureCache.emplace(key, handle);
	return handle;
}

bool Renderer::LoadShaders(bool fromSourceOnly)
{
	std::vector<uint8_t> vsCode, psCode, shadowCode;
	std::wstring vsSource, psSource, shadowSource;

	if (fromSourceOnly)
	{
		vsSource = Paths::GetShaderSourcePath(kVertexShaderFile);
		psSource = Paths::GetShaderSourcePath(kPixelShaderFile);
		shadowSource = Paths::GetShaderSourcePath(kShadowShaderFile);
		if (!ShaderCompiler::CompileFromFile(vsSource, "VS_Main", "vs_5_0", vsCode)) return false;
		if (!ShaderCompiler::CompileFromFile(psSource, "PS_Main", "ps_5_0", psCode)) return false;
		if (!ShaderCompiler::CompileFromFile(shadowSource, "VS_Shadow", "vs_5_0", shadowCode)) return false;
	}
	else
	{
		if (!ShaderCompiler::LoadOrCompile(kVertexShaderFile, "VS_Main", "vs_5_0", vsCode, &vsSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kPixelShaderFile, "PS_Main", "ps_5_0", psCode, &psSource)) return false;
		if (!ShaderCompiler::LoadOrCompile(kShadowShaderFile, "VS_Shadow", "vs_5_0", shadowCode, &shadowSource)) return false;
	}

#if defined(_DEBUG)
	// HLSL cbuffer 크기와 C++ 구조체 크기를 리플렉션으로 대조한다. 어긋나면 실패.
	bool layoutOk = true;
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(vsCode, "PerFrame", sizeof(PerFrameConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(vsCode, "PerObject", sizeof(PerObjectConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(vsCode, "Material", sizeof(MaterialConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(psCode, "PerFrame", sizeof(PerFrameConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(psCode, "Lights", sizeof(LightConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(psCode, "Material", sizeof(MaterialConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(shadowCode, "PerFrame", sizeof(PerFrameConstants));
	layoutOk &= ShaderCompiler::ValidateConstantBufferSize(shadowCode, "PerObject", sizeof(PerObjectConstants));
	if (!layoutOk)
	{
		Log::Error("Renderer : 상수버퍼 레이아웃 불일치.");
		return false;
	}
#endif

	// 셰이더가 실제로 쓰는 슬롯이 선언한 레이아웃 안에 있는지. Release는 경고만.
	if (!ValidateBindings(vsCode, psCode, shadowCode))
	{
#if defined(_DEBUG)
		return false;
#endif
	}

	auto makeShader = [this](ShaderStage stage, const std::vector<uint8_t>& code, const char* name)
	{
		ShaderDesc desc;
		desc.stage = stage;
		desc.bytecode = code.data();
		desc.bytecodeSize = code.size();
		desc.debugName = name;
		return m_device->CreateShader(desc);
	};
	const ShaderHandle newVs = makeShader(ShaderStage::Vertex, vsCode, "BasicVS");
	const ShaderHandle newPs = makeShader(ShaderStage::Pixel, psCode, "BasicPS");
	const ShaderHandle newShadowVs = makeShader(ShaderStage::Vertex, shadowCode, "ShadowVS");

	if (!newVs.IsValid() || !newPs.IsValid() || !newShadowVs.IsValid())
	{
		Log::Error("Renderer : 셰이더 생성 실패.");
		m_device->DestroyShader(newVs);
		m_device->DestroyShader(newPs);
		m_device->DestroyShader(newShadowVs);
		return false;
	}

	// 이전 셰이더는 PSO가 참조로 붙들고 있으므로 풀에서 지워도 캐시가 비워질 때까지 산다.
	m_device->DestroyShader(m_vs);
	m_device->DestroyShader(m_ps);
	m_device->DestroyShader(m_shadowVs);
	m_vs = newVs;
	m_ps = newPs;
	m_shadowVs = newShadowVs;

	// 핫리로드 감시 목록: 소스 트리의 원본 셋. Release에서도 목록은 만들지만 CheckHotReload가 돌지 않는다.
	m_watched.clear();
	for (const wchar_t* file : { kVertexShaderFile, kPixelShaderFile, kShadowShaderFile, kCommonShaderFile })
	{
		WatchedFile watched;
		watched.path = Paths::GetShaderSourcePath(file);
		watched.writeTime = ShaderCompiler::GetLastWriteTime(watched.path);
		m_watched.push_back(watched);
	}

	Log::Info("셰이더 로드: VS %s, PS %s, ShadowVS %s", Log::ToUtf8(vsSource.c_str()).c_str(), Log::ToUtf8(psSource.c_str()).c_str(), Log::ToUtf8(shadowSource.c_str()).c_str());
	return true;
}

bool Renderer::ValidateBindings(const std::vector<uint8_t>& vsCode, const std::vector<uint8_t>& psCode, const std::vector<uint8_t>& shadowVsCode) const
{
	// 선언: 세 레이아웃의 슬롯 합집합. Renderer가 만든 Desc를 그대로 다시 쓴다.
	struct Declared { BindingType type; uint8_t reg; uint8_t stageMask; };
	const Declared declared[] =
	{
		{ BindingType::ConstantBuffer, kCBSlotPerFrame,     ShaderStageMask_Vertex | ShaderStageMask_Pixel },
		{ BindingType::ConstantBuffer, kCBSlotLights,       ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotShadowMap,   ShaderStageMask_Pixel },
		{ BindingType::Sampler,        kSamplerSlotShadow,  ShaderStageMask_Pixel },
		{ BindingType::ConstantBuffer, kCBSlotPerObject,    ShaderStageMask_Vertex },
		{ BindingType::ConstantBuffer, kCBSlotMaterial,     ShaderStageMask_Vertex | ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotAlbedo,      ShaderStageMask_Pixel },
		{ BindingType::ShaderResource, kSRVSlotNormal,      ShaderStageMask_Pixel },
		{ BindingType::Sampler,        kSamplerSlotMaterial, ShaderStageMask_Pixel },
	};

	bool ok = true;
	auto check = [&](const std::vector<uint8_t>& code, uint8_t stage, const char* stageName)
	{
		std::vector<ShaderCompiler::ReflectedBinding> used;
		if (!ShaderCompiler::ReflectBindings(code, used)) return;   // 리플렉션 불가면 검사 생략
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
	return ok;
}

bool Renderer::ReloadShaders()
{
	if (m_device == nullptr) return false;

	Log::Info("셰이더 핫리로드 시작…");
	if (!LoadShaders(true))
	{
		Log::Warn("셰이더 핫리로드 실패. 이전 셰이더를 유지한다.");
		// 실패해도 수정 시각은 갱신해 같은 파일로 계속 재시도하지 않게 한다. 다시 저장하면 다시 시도한다.
		for (WatchedFile& w : m_watched) w.writeTime = ShaderCompiler::GetLastWriteTime(w.path);
		return false;
	}

	// 새 셰이더 핸들로 PSO를 다시 만들어야 한다. 캐시를 비우면 이전 세대 핸들은 무효가 되고,
	// 다음 프레임의 GetPipelineFor가 새 Desc(새 vs/ps 핸들)로 새 PSO를 만든다.
	m_baseDesc.vs = m_vs;
	m_baseDesc.ps = m_ps;
	m_shadowDesc.vs = m_shadowVs;
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
	SetSceneTarget(0, 0);   // 11단계
	m_device->DestroyTexture(m_shadowMap);
	m_device->DestroySampler(m_shadowSampler);
	m_shadowMap = TextureHandle{};
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
	m_device->DestroyBuffer(m_perFrameCB);
	m_device->DestroyBuffer(m_perObjectCB);
	m_device->DestroyBuffer(m_lightCB);
	m_device->DestroyShader(m_vs);
	m_device->DestroyShader(m_ps);
	m_device->DestroyShader(m_shadowVs);
	// PSO는 캐시가 소유한다. 셰이더 핸들을 지워도 PSO는 자기 참조로 셰이더 객체를 붙들고 있다.

	m_perFrameCB = m_perObjectCB = m_lightCB = BufferHandle{};
	m_frameSet = m_objectSet = ResourceSetHandle{};
	m_frameLayout = m_objectLayout = m_materialLayout = BindingLayoutHandle{};
	m_vs = m_ps = m_shadowVs = ShaderHandle{};
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
		// 내장("builtin:") 은 남긴다 — 흰색·평평한 노멀은 프레임/기본 재질이 계속 쓴다.
		for (auto it = m_textureCache.begin(); it != m_textureCache.end();)
		{
			if (it->first.rfind("builtin:", 0) == 0) { ++it; continue; }
			// 로드에 실패해 흰색으로 대체된 항목은 내장 핸들을 공유하므로 파괴하지 않는다.
			if (it->second != m_whiteTexture && it->second != m_flatNormalTexture) m_device->DestroyTexture(it->second);
			it = m_textureCache.erase(it);
		}
	}
}

// ------------------------------------------------------------------ 프레임

XMMATRIX Renderer::ComputeLightViewProj(const LightData& light) const
{
	// 방향광은 위치가 없다. 씬 중심에서 빛의 반대 방향으로 distance 만큼 물러난 곳에 눈을 두고,
	// 직교 투영으로 orthoSize × orthoSize 영역을 본다. 씬 경계를 알면 그 AABB 에 맞추는 것이 다음 단계다.
	XMVECTOR direction = XMLoadFloat3(&light.direction);
	if (XMVectorGetX(XMVector3LengthSq(direction)) < 0.000001f)
	{
		direction = XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f);
	}
	direction = XMVector3Normalize(direction);

	const XMVECTOR center = XMVectorSet(0.0f, 2.0f, 0.0f, 1.0f);
	const XMVECTOR eye = XMVectorSubtract(center, XMVectorScale(direction, m_settings.shadowDistance));
	// 빛이 거의 수직이면 up = +Y 가 시선과 평행해져 LookAt 이 무너진다. 그때는 +Z 를 up 으로.
	const XMVECTOR up = (fabsf(XMVectorGetY(direction)) > 0.99f) ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	const XMMATRIX view = XMMatrixLookAtLH(eye, center, up);
	const float size = m_settings.shadowOrthoSize;
	const XMMATRIX proj = XMMatrixOrthographicLH(size, size, 0.1f, m_settings.shadowDistance * 2.0f + size);
	return view * proj;
}

void Renderer::Render(const Scene& scene, const Camera& camera, float totalTime)
{
	if (m_device == nullptr) return;

	CheckHotReload(totalTime);
	++m_frameNumber;
	const uint32_t reloads = m_stats.shaderReloads;
	m_stats = Stats{};
	m_stats.shaderReloads = reloads;

	RHI::CommandList& cmd = m_device->GetCommandList();

	// ---- 광원 0 (방향광) 의 그림자 행렬 ----
	const std::vector<LightData>& sceneLights = scene.GetLights();
	m_shadowEnabledThisFrame = m_settings.shadows && !sceneLights.empty()
		&& sceneLights[0].type == static_cast<uint32_t>(LightType::Directional);
	const XMMATRIX lightViewProj = m_shadowEnabledThisFrame ? ComputeLightViewProj(sceneLights[0]) : XMMatrixIdentity();
	XMStoreFloat4x4(&m_lightViewProj, lightViewProj);

	// ---- b0 PerFrame ----
	// HLSL은 column_major로 읽고 셰이더는 mul(v, M)을 쓰므로 전치해서 올린다 (ShaderConstants.h).
	const XMMATRIX view = camera.GetViewMatrix();
	const XMMATRIX proj = camera.GetProjectionMatrix();
	PerFrameConstants perFrame = {};
	XMStoreFloat4x4(&perFrame.view, XMMatrixTranspose(view));
	XMStoreFloat4x4(&perFrame.proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&perFrame.viewProj, XMMatrixTranspose(view * proj));
	XMStoreFloat4x4(&perFrame.lightViewProj, XMMatrixTranspose(lightViewProj));
	perFrame.cameraPosition = camera.GetPosition();
	perFrame.time = totalTime;
	perFrame.shadowParams = XMFLOAT4(1.0f / kShadowMapSize, m_settings.shadowBias, m_settings.shadowStrength, m_shadowEnabledThisFrame ? 1.0f : 0.0f);
	perFrame.debugParams = XMFLOAT4(static_cast<float>(m_settings.debugView), m_settings.debugDepthRange, 0.0f, 0.0f);
	m_device->UpdateBuffer(m_perFrameCB, &perFrame, sizeof(perFrame));

	// ---- b2 Lights ----
	LightConstants lights = {};
	lights.lightCount = static_cast<uint32_t>(sceneLights.size() < MAX_LIGHTS ? sceneLights.size() : MAX_LIGHTS);
	lights.ambientColor = scene.ambientColor;
	for (uint32_t i = 0; i < lights.lightCount; ++i)
	{
		lights.lights[i] = sceneLights[i];

		// 방향은 셰이더가 정규화된 값을 기대한다. 0벡터면 아래를 향하게.
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
	m_device->UpdateBuffer(m_lightCB, &lights, sizeof(lights));

	BuildDrawList(scene);

	// 프레임 셋(b0, b2, t8, s4)과 오브젝트 셋(b1). 그림자 패스는 t8(그림자 맵)을 DSV 로 잡으므로
	// BeginRenderPass 가 t8 SRV 를 풀어 준다. 메인 패스가 셋을 다시 바인딩해 t8 을 되살린다.
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

	m_stats.renderPasses = cmd.GetStats().renderPasses;
	m_stats.barriers = cmd.GetStats().barriers;
}

void Renderer::BuildDrawList(const Scene& scene)
{
	m_drawList.clear();
	for (const GameObject& object : scene.GetObjects())
	{
		const Mesh* mesh = object.GetMesh();
		if (mesh == nullptr || !mesh->IsValid()) continue;

		// 9단계: 서브메시마다 DrawItem 하나. 재질은 슬롯 → 오브젝트 기본 → Renderer 기본 순.
		for (const Submesh& submesh : mesh->GetSubmeshes())
		{
			if (submesh.indexCount == 0) continue;
			const Material* material = object.GetMaterialForSlot(submesh.materialSlot);
			if (material == nullptr) material = &m_defaultMaterial;

			DrawItem item;
			item.pipeline = GetPipelineFor(*material, mesh->GetVertexFormat());
			item.material = material;
			item.mesh = mesh;
			item.object = &object;
			item.indexStart = submesh.indexStart;
			item.indexCount = submesh.indexCount;
			if (!item.pipeline.IsValid()) continue;
			m_drawList.push_back(item);
		}
	}

	// ---- 정렬: PSO → Material → Mesh → 인덱스 범위. 같은 상태가 연속되면 바인딩을 건너뛸 수 있다 ----
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
	// 앞 드로우가 아직 실행되지 않았어도 덮어쓰지 않는다. 바인딩은 프레임 초에 한 번 했다.
	const XMMATRIX world = object.GetTransform().GetWorldMatrix();
	PerObjectConstants perObject = {};
	XMStoreFloat4x4(&perObject.world, XMMatrixTranspose(world));
	// 노멀 변환 행렬은 (W⁻¹)ᵀ. 전치해서 올려야 하므로 ((W⁻¹)ᵀ)ᵀ = W⁻¹ 를 그대로 저장한다.
	XMStoreFloat4x4(&perObject.worldInvTranspose, XMMatrixInverse(nullptr, world));
	m_device->UpdateBuffer(m_perObjectCB, &perObject, sizeof(perObject));
}

void Renderer::RenderShadowPass(RHI::CommandList& cmd)
{
	// 그림자 맵: ShaderResource(또는 Common) → DepthWrite. D3D11 에서는 추적만, D3D12 에서는 실제 배리어.
	cmd.Barrier(m_shadowMap, m_shadowMapState, ResourceState::DepthWrite);
	m_shadowMapState = ResourceState::DepthWrite;

	RenderPassDesc pass;
	pass.colorCount = 0;
	pass.depth.texture = m_shadowMap;
	pass.depth.load = LoadOp::Clear;
	pass.depth.clearDepth = 1.0f;
	pass.debugName = "ShadowPass";
	cmd.BeginRenderPass(pass);   // 뷰포트는 2048² (attachment 크기)

	PipelineHandle lastPipeline;
	const Mesh* lastMesh = nullptr;
	for (const DrawItem& item : m_drawList)
	{
		if (item.material->unlit) continue;   // 감마 검증 카드처럼 조명을 받지 않는 물체는 그림자도 만들지 않는다

		const PipelineHandle pipeline = GetShadowPipelineFor(*item.material, item.mesh->GetVertexFormat());
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

	cmd.EndRenderPass();

	// DepthWrite → ShaderResource. 메인 패스가 t8 로 읽는다.
	cmd.Barrier(m_shadowMap, ResourceState::DepthWrite, ResourceState::ShaderResource);
	m_shadowMapState = ResourceState::ShaderResource;
}

void Renderer::RenderMainPass(RHI::CommandList& cmd, const Scene& scene)
{
	// 11단계: 에디터가 씬 뷰 텍스처를 요청했으면 거기에, 아니면 백버퍼에 직접.
	TextureHandle target;
	TextureHandle depth;
	if (IsOffscreen())
	{
		target = m_sceneColor;
		depth = m_sceneDepth;
		cmd.Barrier(m_sceneColor, m_sceneColorState, ResourceState::RenderTarget);
		m_sceneColorState = ResourceState::RenderTarget;
	}
	else
	{
		target = m_device->GetBackBuffer();
		depth = m_device->GetDepthBuffer();
		// 백버퍼: Present → RenderTarget. (D3D12 스왑체인 관례. D3D11 에서는 추적만.)
		cmd.Barrier(target, ResourceState::Present, ResourceState::RenderTarget);
	}

	RenderPassDesc pass;
	pass.colorCount = 1;
	pass.colors[0].texture = target;
	pass.colors[0].load = LoadOp::Clear;
	pass.colors[0].srgbView = m_settings.srgbOutput;   // 클리어 색은 선형 값. sRGB 뷰가 인코딩한다
	for (int i = 0; i < 4; ++i) pass.colors[0].clearColor[i] = scene.clearColor[i];
	pass.depth.texture = depth;
	pass.depth.load = LoadOp::Clear;
	pass.debugName = "MainPass";
	cmd.BeginRenderPass(pass);

	// 그림자 패스가 t8 을 풀었으므로 프레임 셋을 다시 건다 (이제 그림자 맵은 SRV 상태).
	cmd.SetResourceSet(m_frameSet);
	cmd.SetResourceSet(m_objectSet);

	// ---- 드로우. "직전" 기억은 이 패스 안에서만 산다 ----
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

		// 9단계: 서브메시 범위만 그린다. 같은 메시의 다른 서브메시는 정렬 덕에 대개 바로 뒤에 온다 (VB/IB 재바인딩 없음).
		UploadObjectConstants(*item.object);
		cmd.DrawIndexed(item.indexCount, item.indexStart, 0);
		m_stats.triangles += item.indexCount / 3;
		++m_stats.draws;
	}

	cmd.EndRenderPass();

	if (IsOffscreen())
	{
		// ImGui 가 이 텍스처를 샘플한다. D3D12 에서는 실제 배리어, D3D11 에서는 추적.
		cmd.Barrier(m_sceneColor, ResourceState::RenderTarget, ResourceState::ShaderResource);
		m_sceneColorState = ResourceState::ShaderResource;
	}
}

void Renderer::BeginUIPass()
{
	if (m_device == nullptr) return;
	// ImGui 는 sRGB 로 인코딩된 색을 그대로 내므로 UNORM 뷰에 그린다. 씬 위에 얹으므로 Load, 깊이 없음.
	RenderPassDesc pass;
	pass.colorCount = 1;
	pass.colors[0].texture = m_device->GetBackBuffer();
	pass.colors[0].load = LoadOp::Load;
	pass.colors[0].srgbView = false;
	pass.debugName = "UIPass";
	if (IsOffscreen())
	{
		// 씬이 백버퍼에 그려지지 않았으므로 여기서 전이하고 지운다 (에디터 배경).
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

	// 정점/인덱스 버퍼는 바뀌지 않으므로 Default + 초기 데이터.
	GpuMesh gpuMesh;
	gpuMesh.vertexFormat = mesh.GetVertexFormat();

	BufferDesc vbDesc;
	vbDesc.size = static_cast<uint32_t>(sizeof(VERTEX) * mesh.GetVertices().size());
	vbDesc.usage = BufferUsage::Default;
	vbDesc.bindFlags = BufferBind_Vertex;
	vbDesc.stride = GetVertexStride(mesh.GetVertexFormat());
	vbDesc.debugName = "VB";
	gpuMesh.vertexBuffer = m_device->CreateBuffer(vbDesc, mesh.GetVertices().data());

	// 9단계 (D10): 정점이 65,536개 미만이면 16비트 인덱스. 버퍼와 IA 대역폭이 절반이다. CPU 쪽은 32비트 그대로.
	BufferDesc ibDesc;
	ibDesc.usage = BufferUsage::Default;
	ibDesc.bindFlags = BufferBind_Index;
	ibDesc.debugName = "IB";
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

		BufferDesc cbDesc;
		cbDesc.size = sizeof(MaterialConstants);
		cbDesc.usage = BufferUsage::Dynamic;
		cbDesc.bindFlags = BufferBind_Constant;
		cbDesc.debugName = "CB_Material";
		created.constants = m_device->CreateBuffer(cbDesc);
		if (!created.constants.IsValid())
		{
			Log::Error("GpuMaterial 상수버퍼 생성 실패 (%s).", material.name.c_str());
			return nullptr;
		}
		gpuMaterial = &m_gpuMaterials.emplace(&material, created).first->second;
	}

	// 재질이 가리키는 텍스처·샘플러가 바뀌었으면(ImGui 편집, 첫 생성) ResourceSet을 다시 만든다.
	// ResourceSet은 값이라 갱신이 없다 — D3D12 디스크립터 테이블도 다시 써야 한다.
	const TextureHandle albedo = GetOrLoadTexture(material.albedoTexture, material.albedoSrgb, scene);
	const TextureHandle normal = material.normalTexture.empty() ? m_flatNormalTexture : GetOrLoadTexture(material.normalTexture, false, scene);
	const SamplerPreset preset = (m_settings.samplerOverride >= 0 && m_settings.samplerOverride < static_cast<int>(kSamplerPresetCount))
		? static_cast<SamplerPreset>(m_settings.samplerOverride) : material.sampler;
	const SamplerHandle sampler = GetSampler(preset);
	if (!gpuMaterial->set.IsValid() || gpuMaterial->boundAlbedo != albedo || gpuMaterial->boundNormal != normal || gpuMaterial->boundSampler != sampler)
	{
		m_device->DestroyResourceSet(gpuMaterial->set);
		ResourceSetDesc setDesc;
		setDesc.layout = m_materialLayout;
		setDesc.bindings[0].buffer = gpuMaterial->constants;
		setDesc.bindings[1].texture = albedo;
		setDesc.bindings[2].texture = normal;
		setDesc.bindings[3].sampler = sampler;
		setDesc.debugName = "Set_Material";
		gpuMaterial->set = m_device->CreateResourceSet(setDesc);
		gpuMaterial->boundAlbedo = albedo;
		gpuMaterial->boundNormal = normal;
		gpuMaterial->boundSampler = sampler;
		if (!gpuMaterial->set.IsValid())
		{
			Log::Error("GpuMaterial ResourceSet 생성 실패 (%s).", material.name.c_str());
			return nullptr;
		}
	}

	// 재질 상수는 프레임에 한 번 올린다. 편집(ImGui)이 바로 반영되도록 매 프레임 올리되,
	// 같은 재질을 여러 오브젝트가 쓰면 그중 첫 드로우에서만.
	if (gpuMaterial->uploadedFrame != m_frameNumber)
	{
		MaterialConstants constants = {};
		constants.baseColor = material.baseColor;
		constants.specularColor = material.specularColor;
		constants.shininess = material.shininess;
		constants.uvScale = material.uvScale;
		constants.unlit = material.unlit ? 1.0f : 0.0f;
		constants.normalStrength = m_settings.normalMapping ? material.normalStrength : 0.0f;
		constants.alphaCutoff = material.alphaCutoff;
		m_device->UpdateBuffer(gpuMaterial->constants, &constants, sizeof(constants));
		gpuMaterial->uploadedFrame = m_frameNumber;
	}
	return gpuMaterial;
}

PipelineHandle Renderer::GetPipelineFor(const Material& material, VertexFormat vertexFormat)
{
	PipelineStateDesc desc = m_baseDesc;
	desc.vertexLayout = GetVertexLayout(vertexFormat);
	desc.rasterizer.fill = (m_settings.wireframe || material.wireframe) ? FillMode::Wireframe : FillMode::Solid;
	desc.rasterizer.cull = (m_settings.cullBack && !material.doubleSided) ? CullMode::Back : CullMode::None;
	// 8단계: D3D12 PSO 는 렌더 타깃 뷰의 포맷을 알아야 한다. 메인 패스가 백버퍼의 sRGB 뷰에 그리면 PSO 도 sRGB.
	// D3D11 은 이 항목을 쓰지 않지만 1단계부터 Desc 에 있었고, 여기서 처음 실제 값이 들어간다.
	desc.rtvFormats[0] = m_settings.srgbOutput ? Format::R8G8B8A8_UNORM_SRGB : Format::R8G8B8A8_UNORM;
	desc.rtvCount = 1;
	desc.dsvFormat = Format::D24_UNORM_S8_UINT;
	return m_device->CreatePipeline(desc);
}

PipelineHandle Renderer::GetShadowPipelineFor(const Material& material, VertexFormat vertexFormat)
{
	// 그림자는 항상 솔리드로 그린다 (와이어프레임 재질도 실체는 있다). 양면 재질은 컬링 없이.
	PipelineStateDesc desc = m_shadowDesc;
	desc.vertexLayout = GetVertexLayout(vertexFormat);
	desc.rasterizer.cull = material.doubleSided ? CullMode::None : CullMode::Back;
	return m_device->CreatePipeline(desc);
}

// ------------------------------------------------------------------ 11단계: 오프스크린 씬 뷰

void Renderer::SetSceneTarget(uint32_t width, uint32_t height)
{
	if (m_device == nullptr) return;
	if (width == m_sceneWidth && height == m_sceneHeight) return;

	// 이전 텍스처는 Destroy — D3D12 는 지연 해제(실행 중인 프레임의 ImGui 드로우가 아직 읽을 수 있다), D3D11 은 참조 카운트.
	if (m_sceneColor.IsValid()) m_device->DestroyTexture(m_sceneColor);
	if (m_sceneDepth.IsValid()) m_device->DestroyTexture(m_sceneDepth);
	m_sceneColor = TextureHandle{};
	m_sceneDepth = TextureHandle{};
	m_sceneWidth = width;
	m_sceneHeight = height;
	m_sceneColorState = ResourceState::Common;
	if (width == 0 || height == 0) return;

	TextureDesc color;
	color.width = width;
	color.height = height;
	color.format = Format::R8G8B8A8_UNORM;   // TYPELESS 리소스: sRGB RTV + UNORM SRV
	color.mipLevels = 1;
	color.bindFlags = TextureBind_RenderTarget | TextureBind_ShaderResource;
	color.debugName = "SceneView_Color";
	m_sceneColor = m_device->CreateTexture(color);

	TextureDesc depth;
	depth.width = width;
	depth.height = height;
	depth.format = Format::D24_UNORM_S8_UINT;
	depth.mipLevels = 1;
	depth.bindFlags = TextureBind_DepthStencil;
	depth.debugName = "SceneView_Depth";
	m_sceneDepth = m_device->CreateTexture(depth);

	if (!m_sceneColor.IsValid() || !m_sceneDepth.IsValid())
	{
		Log::Error("씬 뷰 텍스처 생성 실패 (%ux%u).", width, height);
		SetSceneTarget(0, 0);
		return;
	}
	Log::Info("씬 뷰 텍스처 %ux%u", width, height);
}

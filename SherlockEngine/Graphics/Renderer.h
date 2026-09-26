#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <DirectXMath.h>
#include "RHI/Handle.h"
#include "RHI/PipelineTypes.h"
#include "RHI/RenderPassTypes.h"
#include "Graphics/VertexTypes.h"   // VertexFormat
#include "Scene/Material.h"
#include "Scene/Light.h"

namespace RHI { class Device; class CommandList; }
class Scene;
class Camera;
class Mesh;
class GameObject;

// ImGui에서 바꾸는 렌더 설정. Material의 옵션과 합쳐져 PipelineStateDesc가 됨.
struct RenderSettings
{
	bool wireframe = false;
	bool cullBack = true;
	bool srgbOutput = true;        // 5단계: 백버퍼 sRGB 뷰. 끄면 감마 보정 없이 출력함 (비교용)
	int samplerOverride = -1;      // -1 = 재질 설정대로. 그 외 SamplerPreset 값으로 전부 덮어씀

	// 6단계: 그림자 (방향광 0) 와 노멀 매핑
	bool shadows = true;
	float shadowBias = 0.0015f;     // 수신자 깊이에서 빼는 값 (광원 클립 z 단위, 0..1)
	float shadowStrength = 1.0f;    // 0 = 그림자 없음, 1 = 완전히 어둡게
	float shadowOrthoSize = 60.0f;  // 광원 직교 투영의 한 변 (월드 단위). 씬 전체를 덮어야 함
	float shadowDistance = 40.0f;   // 광원 눈의 위치 = 씬 중심 − 방향 × 거리
	bool normalMapping = true;

	// 11단계: 디버그 뷰 (0 조명, 1 알베도, 2 월드 노멀, 3 깊이, 4 그림자 계수, 5 UV) 와 깊이 뷰의 범위(월드 단위)
	int debugView = 0;
	float debugDepthRange = 60.0f;
};

// 씬을 그림. 씬 데이터는 갖지 않고 입력으로 받음 (rendering-analysis D3).
//
// 소유하는 것: 셰이더 핸들, 상수버퍼 핸들, BindingLayout 세 개(프레임/오브젝트/재질)와
// ResourceSet, PSO Desc의 고정 부분, Mesh → GPU 버퍼 캐시, Material → GPU 재질 캐시,
// 텍스처 캐시(이름 → 핸들), 샘플러 프리셋, 그림자 맵. 전부 핸들이므로 이 헤더와 .cpp에는 D3D 타입이 없음.
//
// 6단계: 모든 GPU 명령은 Device::GetCommandList() 를 거침. 프레임은 렌더 패스 세 개로 이루어짐.
//   ShadowPass (깊이 전용, 2048², 광원 ViewProj) → MainPass (백버퍼 sRGB 뷰 + 깊이) → UIPass (백버퍼 UNORM 뷰)
// 그림자 맵은 "DSV 로 쓰고 → SRV 로 읽는" 첫 리소스라 양쪽에 Barrier 호출이 있음 (D3D11 에서는 상태 추적만 함).
class Renderer
{
public:
	struct Stats
	{
		uint32_t draws = 0;
		uint32_t shadowDraws = 0;
		uint32_t triangles = 0;        // 9단계: 메인 패스 삼각형 수
		uint32_t pipelineSwitches = 0;
		uint32_t resourceSetSwitches = 0;
		uint32_t meshSwitches = 0;
		uint32_t renderPasses = 0;
		uint32_t barriers = 0;
		uint32_t shaderReloads = 0;   // 누적
	};

	static constexpr uint32_t kShadowMapSize = 2048;

	Renderer();
	~Renderer();

	bool Initialize(RHI::Device* device);
	void Shutdown();

	// 그림자 패스 + 메인 패스. 프레임의 UI 패스에서는 아래 두 함수 사이에 ImGui 를 그림.
	void Render(const Scene& scene, const Camera& camera, float totalTime);
	void BeginUIPass();   // 백버퍼 UNORM 뷰, 깊이 없음, Load (씬 위에 그림)
	void EndUIPass();     // 패스 종료 + 백버퍼 RenderTarget → Present 전이

	// 셰이더를 소스에서 다시 컴파일하고 PSO 캐시를 비움. 실패하면 이전 셰이더를 유지함.
	// Debug 빌드에서는 소스 파일 수정 시각을 감시해 자동으로 호출함. 수동 호출도 가능(F4).
	bool ReloadShaders();

	RenderSettings& GetSettings() { return m_settings; }
	const Stats& GetStats() const { return m_stats; }
	size_t GetPipelineCount() const;
	size_t GetGpuMeshCount() const { return m_gpuMeshes.size(); }
	size_t GetGpuMaterialCount() const { return m_gpuMaterials.size(); }
	size_t GetTextureCount() const { return m_textureCache.size(); }
	TextureHandle GetShadowMap() const { return m_shadowMap; }   // 디버그 표시용

	// 11단계: 씬 뷰를 오프스크린 텍스처에 그림 (에디터용). 0×0 이면 백버퍼에 직접 그림 (기존 동작).
	// 크기가 바뀌면 텍스처를 다시 만듦. 색 텍스처는 UNORM(TYPELESS) — sRGB 뷰로 그리고 ImGui 는 UNORM SRV 로 읽음.
	void SetSceneTarget(uint32_t width, uint32_t height);
	bool IsOffscreen() const { return m_sceneColor.IsValid(); }
	TextureHandle GetSceneTexture() const { return m_sceneColor; }
	uint32_t GetSceneWidth() const { return m_sceneWidth; }
	uint32_t GetSceneHeight() const { return m_sceneHeight; }

	// 11-C단계: 카메라 미리보기. 에디터가 선택한 카메라 오브젝트의 시점으로 메인 패스를 한 번 더 (같은 그림자 맵, 같은 드로우 목록으로)
	// 작은 텍스처에 그림. 카메라 포인터는 Render 가 끝날 때까지 유효해야 함. nullptr 이거나 0×0 이면 그리지 않음.
	void SetPreviewTarget(uint32_t width, uint32_t height);
	void SetPreviewCamera(const Camera* camera) { m_previewCamera = camera; }
	TextureHandle GetPreviewTexture() const { return m_previewColor; }

	// 메시/재질이 파괴되거나 내용이 바뀌면 캐시 항목을 버림.
	void InvalidateMesh(const Mesh* mesh);
	void InvalidateMaterial(const Material* material);
	// 9단계: 씬의 모든 메시·재질 캐시를 버림 (Scene::Clear 전에 호출). releaseTextures 가 true 면 내장이 아닌 텍스처도 버림.
	void InvalidateScene(const Scene& scene, bool releaseTextures);

private:
	struct GpuMesh
	{
		BufferHandle vertexBuffer;
		BufferHandle indexBuffer;
		uint32_t indexCount = 0;
		Format indexFormat = Format::R32_UINT;   // 9단계 (D10): 정점 65,536개 미만이면 R16_UINT
		VertexFormat vertexFormat = VertexFormat::PositionColorNormalTexcoordTangent;
	};

	// Material = 상수버퍼 + ResourceSet(b3, t0, t1, s0). PSO는 Desc가 캐시 키이므로 여기 저장하지
	// 않고 매 프레임 GetPipelineFor로 얻음(핫리로드로 캐시가 비워져도 안전함).
	// 셋에 들어간 텍스처·샘플러가 재질 편집으로 바뀌면 셋을 다시 만듦.
	struct GpuMaterial
	{
		BufferHandle constants;
		ResourceSetHandle set;
		TextureHandle boundAlbedo;
		TextureHandle boundNormal;
		SamplerHandle boundSampler;
		uint64_t uploadedFrame = UINT64_MAX;   // 이번 프레임에 상수를 올렸는지
	};

	struct DrawItem
	{
		PipelineHandle pipeline;   // 메인 패스 PSO (정렬 키)
		const Material* material;
		const Mesh* mesh;
		const GameObject* object;
		uint32_t indexStart = 0;   // 9단계: 서브메시 범위
		uint32_t indexCount = 0;
	};

	bool LoadShaders(bool fromSourceOnly);
	bool ValidateBindings(const std::vector<uint8_t>& vsCode, const std::vector<uint8_t>& psCode, const std::vector<uint8_t>& shadowVsCode) const;
	void CheckHotReload(float totalTime);
	bool CreateSamplers();
	bool CreateBuiltinTextures();
	bool CreateShadowResources();

	void BuildDrawList(const Scene& scene);
	void UploadPerFrameConstants(const Camera& camera, float totalTime);   // b0: view/proj 는 이 카메라 기준, 나머지는 프레임 공통 값
	void RenderShadowPass(RHI::CommandList& cmd);
	void RenderMainPass(RHI::CommandList& cmd, const Scene& scene);
	void RenderPreviewPass(RHI::CommandList& cmd, const Scene& scene);    // 11-C단계
	void DrawItems(RHI::CommandList& cmd, const Scene& scene);            // 메인/미리보기 패스 공통 드로우 루프
	void UploadObjectConstants(const GameObject& object);
	DirectX::XMMATRIX ComputeLightViewProj(const LightData& light) const;

	const GpuMesh* GetOrCreateGpuMesh(const Mesh& mesh);
	const GpuMaterial* GetOrCreateGpuMaterial(const Material& material, const Scene* scene);
	PipelineHandle GetPipelineFor(const Material& material, VertexFormat vertexFormat);
	PipelineHandle GetShadowPipelineFor(const Material& material, VertexFormat vertexFormat);
	// 재질의 텍스처 이름 → 핸들. "builtin:*"은 절차적으로 만들고, 씬에 같은 이름의 이미지가 있으면 그 바이트에서(9단계),
	// 없으면 Assets/Textures/ 의 파일에서 읽음. 실패하면 흰색 텍스처를 돌려줌.
	TextureHandle GetOrLoadTexture(const std::string& name, bool srgb, const Scene* scene);
	SamplerHandle GetSampler(SamplerPreset preset) const;

private:
	RHI::Device* m_device = nullptr;

	ShaderHandle m_vs;
	ShaderHandle m_ps;
	ShaderHandle m_shadowVs;        // 6단계: 깊이 전용 VS
	BufferHandle m_perFrameCB;
	BufferHandle m_perObjectCB;
	BufferHandle m_lightCB;

	// 갱신 빈도별 레이아웃. PSO Desc에는 이 세 개가 순서대로 들어감.
	BindingLayoutHandle m_frameLayout;      // b0 (VS|PS), b2 (PS), t8 그림자 맵 (PS), s4 비교 샘플러 (PS)
	BindingLayoutHandle m_objectLayout;     // b1 (VS)
	BindingLayoutHandle m_materialLayout;   // b3 (VS|PS), t0 알베도 (PS), t1 노멀 (PS), s0 (PS)
	ResourceSetHandle m_frameSet;
	ResourceSetHandle m_objectSet;

	PipelineStateDesc m_baseDesc;     // 메인 패스: 셰이더·레이아웃 등 설정과 무관한 부분
	PipelineStateDesc m_shadowDesc;   // 그림자 패스: VS만, RTV 0개, DSV D32_FLOAT, 깊이 바이어스
	RenderSettings m_settings;
	Stats m_stats;
	uint64_t m_frameNumber = 0;

	Material m_defaultMaterial;     // GameObject.material == nullptr 일 때

	// 5단계: 샘플러 프리셋과 텍스처 캐시
	static constexpr uint32_t kSamplerPresetCount = 5;
	SamplerHandle m_samplers[kSamplerPresetCount];
	TextureHandle m_whiteTexture;   // "텍스처 없음"의 기본값
	TextureHandle m_flatNormalTexture;
	std::unordered_map<std::string, TextureHandle> m_textureCache;   // 키 = 이름 + "|srgb" / "|linear"

	// 6단계: 그림자 맵 (D32_FLOAT, DSV + SRV) 과 비교 샘플러. 상태는 Barrier 로 전이함.
	TextureHandle m_shadowMap;
	SamplerHandle m_shadowSampler;
	ResourceState m_shadowMapState = ResourceState::Common;

	// 11단계: 오프스크린 씬 타깃
	TextureHandle m_sceneColor;
	TextureHandle m_sceneDepth;
	uint32_t m_sceneWidth = 0;
	uint32_t m_sceneHeight = 0;
	ResourceState m_sceneColorState = ResourceState::Common;
	// 11-C단계: 카메라 미리보기 타깃
	TextureHandle m_previewColor;
	TextureHandle m_previewDepth;
	uint32_t m_previewWidth = 0;
	uint32_t m_previewHeight = 0;
	ResourceState m_previewColorState = ResourceState::Common;
	const Camera* m_previewCamera = nullptr;
	bool m_shadowEnabledThisFrame = false;
	DirectX::XMFLOAT4X4 m_lightViewProj;

	// Scene이 메시·재질을 unique_ptr로 소유하므로 포인터가 안정적임. 그래서 키로 씀.
	std::unordered_map<const Mesh*, GpuMesh> m_gpuMeshes;
	std::unordered_map<const Material*, GpuMaterial> m_gpuMaterials;
	std::vector<DrawItem> m_drawList;

	// 핫리로드: 감시하는 소스 파일과 마지막 수정 시각
	struct WatchedFile
	{
		std::wstring path;
		uint64_t writeTime = 0;
	};
	std::vector<WatchedFile> m_watched;
	float m_nextWatchTime = 0.0f;
};

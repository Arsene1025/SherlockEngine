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
#include "Graphics/TextureCache.h"
#include "RHI/BindingTypes.h"       // BindingLayoutDesc (레이아웃 선언 사본)
#include "Scene/Material.h"
#include "Scene/Camera.h"   // 캐스케이드 고정용 카메라 사본 (m_cascadeFreezeCamera)
#include "Scene/Light.h"
#include "Scene/PostProcessSettings.h"
#include "Scene/ShadowSettings.h"

namespace RHI { class Device; class CommandList; }
class Scene;
class DebugDraw;
struct SceneEnvironment;
class Mesh;
class GameObject;
struct PostParamsConstants;   // ShaderConstants.h
struct GpuLight;              // ShaderConstants.h (조명 하나의 GPU 배치)

// ImGui에서 바꾸는 렌더 설정. Material의 옵션과 합쳐져 PipelineStateDesc가 됨.
struct RenderSettings
{
	bool wireframe = false;
	bool cullBack = true;
	bool srgbOutput = true;        // 최종 출력(톤 패스의 RTV)을 sRGB 뷰로 씀. 끄면 감마 보정 없이 출력함 (비교용). 씬 패스는 FP16 선형이라 무관함
	int samplerOverride = -1;      // -1 = 재질 설정대로. 그 외 SamplerPreset 값으로 전부 덮어씀

	// 그림자 (켜고 끄는 스위치. 조명마다는 Light::castShadows) 와 노멀 매핑. 그림자 값(바이어스·세기·노말
	// 오프셋·직교 크기·거리)은 씬 파일에 들어가도록 SceneEnvironment::shadow 에 둠. 여기에는 켜고 끄는
	// 스위치만 있음.
	bool shadows = true;
	bool normalMapping = true;

	// 디버그 뷰 (0 조명, 1 알베도, 2 월드 노멀, 3 깊이, 4 그림자 계수, 5 UV) 와 깊이 뷰의 범위(월드 단위)
	int debugView = 0;
	float debugDepthRange = 60.0f;

	// 본 디버그 표시 (스킨 메시만. 재생 중에는 Animator 의 포즈, 편집 모드에서는 기본 자세): 0 끔, 1 본 선 + 관절 십자, 2 = 1 + 조인트 로컬 축(X 빨강·Y 초록·Z 파랑).
	// 깊이 테스트 없이 메시 위에 그림. 에디터는 Render Settings 와 B 키, 게임은 B 키(끔 → 1 → 2 → 끔).
	int debugBones = 0;
};

// 씬을 그림. 씬 데이터는 갖지 않고 입력으로 받음
//
// 소유하는 것: 셰이더 핸들, 상수버퍼 핸들, BindingLayout 네 개(프레임/오브젝트/재질/후처리)와
// ResourceSet, PSO Desc의 고정 부분, Mesh → GPU 버퍼 캐시, Material → GPU 재질 캐시,
// 텍스처 캐시(TextureCache), 샘플러 프리셋, 그림자 맵, 뷰 타깃(색·깊이·HDR). 전부 핸들이므로 이 헤더와
// .cpp에는 D3D 타입이 없음.
//
// 구현은 파일 넷으로 나뉨:
//   Renderer.cpp         초기화, 셰이더 표, 프레임 흐름(Render), 드로우 목록·컬링, GPU 캐시, PSO
//   RendererShadow.cpp   그림자 뷰 계산(방향광·CSM·스팟·포인트), 아틀라스 패스
//   RendererTargets.cpp  뷰 타깃(메인·카메라 미리보기) 생성·해제, UI 패스
//   RendererPost.cpp     HDR 구간 후처리(블러·블룸), 톤 패스, 단계별 썸네일
//
// 모든 GPU 명령은 Device::GetCommandList() 를 거침. 프레임은 아래 순서의 렌더 패스로 이루어짐.
//   1. ShadowPass   깊이 전용. 지역광 아틀라스(2048²)와 CSM 아틀라스(2048²)에 뷰(조명·캐스케이드·큐브 면)마다
//                   뷰포트를 바꿔 그림
//   2. MainPass     FP16 HDR 텍스처 + 깊이
//   3. TonemapPass  노출·ACES·sRGB → 씬 뷰 텍스처 또는 백버퍼. 톤매핑은 셰이더 끝이 아니라 별도 패스임
//   4. [PreviewPass → PreviewTonemapPass]  카메라 미리보기용 (Hierarchy 에서 카메라 오브젝트를 골랐을 때)
//   5. UIPass       백버퍼 UNORM 뷰
// MainPass 는 불투명 드로우 뒤에 스카이박스(전체 화면 삼각형, 깊이 1.0)를 그림. 스카이박스는 프레임 셋 t9,
// IBL(확산 조도·반사 프리필터·BRDF LUT)은 t10~t12 에 꽂힘. 재질은 PBR(metallic-roughness)이고 재질 셋이 t0~t4 를 씀.
// 그림자 맵은 "DSV 로 쓰고 → SRV 로 읽는" 첫 리소스라 양쪽에 Barrier 호출이 있음 (D3D11 에서는 상태 추적만 함).
class Renderer
{
public:
	struct Stats
	{
		uint32_t draws = 0;
		uint32_t shadowDraws = 0;
		uint32_t triangles = 0;        // 메인 패스 삼각형 수
		uint32_t pipelineSwitches = 0;
		uint32_t resourceSetSwitches = 0;
		uint32_t meshSwitches = 0;
		uint32_t renderPasses = 0;
		uint32_t barriers = 0;
		uint32_t shaderReloads = 0;   // 누적
		uint32_t shadowViews = 0;     // 이번 프레임에 그린 그림자 뷰(아틀라스 타일) 수
		uint32_t shadowCulled = 0;    // 그 뷰의 절두체나 지역광 범위 밖이라 그림자 패스에서 건너뛴 (뷰, 드로우) 쌍 수
		uint32_t culled = 0;          // 카메라 절두체 밖이라 메인·미리보기 패스에서 건너뛴 드로우 수
	};

	// 2048² 메인 아틀라스는 지역광(스팟·포인트) 전용: 512² 타일 16개를 4×4 로 배치 (D32 약 16MB). 스팟 =
	// 1타일, 포인트 = 연속 6타일. 방향광(캐스케이드 1~4 모두)은 이 아틀라스가 아니라 아래의 CSM 전용
	// 아틀라스에 그림.
	static constexpr uint32_t kShadowAtlasSize = 2048;
	static constexpr uint32_t kShadowTileSize = 512;             // 지역광 타일
	static constexpr uint32_t kShadowLocalTileCount = 16;
	static constexpr uint32_t kShadowPcfBorder = 3;              // PCF 5×5 반경 2 + 쌍선형 1 (텍셀)

	// 방향광 전용 아틀라스. 캐스케이드 2~4개면 아래 그림처럼 1024² 칸 네 개, 단일 뷰(캐스케이드 1)면 2048²
	// 전체를 한 뷰가 씀.
	//   ┌─────────┬─────────┐
	//   │ C0      │ C1      │   캐스케이드 네 장 모두 1024²
	//   │ 1024²   │ 1024²   │
	//   ├─────────┼─────────┤
	//   │ C2      │ C3      │
	//   │ 1024²   │ 1024²   │
	//   └─────────┴─────────┘   2048² (D32 약 16MB)
	// 균등 해상도: 분할(practical split)이 "캐스케이드마다 해상도가 같다"는 전제로 화면 픽셀당 텍셀 밀도를 맞추기 때문.
	// 해상도를 바꾸려면 kCsmCascadeSize 와 CsmRect(Renderer.cpp)의 배치를 같이 고칠 것.
	static constexpr uint32_t kCsmAtlasWidth = 2048;
	static constexpr uint32_t kCsmAtlasHeight = 2048;
	static constexpr uint32_t kCsmCascadeSize[4] = { 1024, 1024, 1024, 1024 };

	// 그림자 뷰 하나 = 아틀라스의 사각형 하나 + 그 사각형에 그릴 광원 행렬. 조명마다 0개 이상 (방향광 1, 스팟
	// 1, 포인트 6, 캐스케이드 N).
	struct ShadowView
	{
		DirectX::XMFLOAT4X4 viewProj;          // 행우선 (올릴 때 전치)
		uint32_t x = 0, y = 0, size = 0;       // 아틀라스 안의 픽셀 사각형 (정사각형)
		uint32_t atlas = 0;                    // 0 = 메인 아틀라스(m_shadowMap), 1 = CSM 아틀라스(m_csmMap)
		DirectX::XMFLOAT4 params = {};         // b6 의 params 와 같은 뜻
		DirectX::XMFLOAT4 cullSphere = {};     // 단계 6. xyz = 광원 위치, w = 범위. w = 0 이면 컬링 안 함
		uint32_t lightIndex = 0;
	};
	uint32_t GetShadowViewCount() const { return m_shadowViewCount; }
	const ShadowView& GetShadowView(uint32_t index) const { return m_shadowViews[index]; }
	uint32_t GetSkippedShadowLights() const { return m_skippedShadowLights; }
	uint32_t GetUsedLocalTiles() const { return m_usedLocalTiles; }

	// ---- CSM 디버그: 캐스케이드 카메라 고정 ----
	// 켜면 그 순간 camera 의 위치·방향·렌즈를 사본으로 잡고, 이후 캐스케이드(분할·광원 뷰·PS 의 구간 판정)를
	// 항상 그 사본 기준으로 계산함. 에디터 카메라를 자유롭게 옮겨서 "고정된 카메라에서 본 거리 구간"을
	// 바깥에서 볼 수 있음. 끄면 다시 매 프레임 현재 카메라를 따라감. 런타임 디버그 상태일 뿐이라 씬 파일에는
	// 저장하지 않음. 카메라는 호출자가 직접 넘김 (Render 는 한 프레임에 여러 번 불려 미리보기 카메라를 잡을
	// 수 있음).
	void SetCascadeFreeze(bool freeze, const Camera& camera);
	bool IsCascadeFrozen() const { return m_cascadeFrozen; }
	// 이번 프레임의 캐스케이드별 절두체 조각 꼭짓점 8개: [0..3] 앞면 (−x−y, +x−y, −x+y, +x+y), [4..7] 뒷면
	// 같은 순서. 월드 좌표. GetCascadeFrustumCount() 가 0 이면 이번 프레임에 CSM 을 안 쓴 것.
	uint32_t GetCascadeFrustumCount() const { return m_cascadeCount; }
	const DirectX::XMFLOAT3* GetCascadeFrustumCorners(uint32_t cascade) const { return m_cascadeCorners[cascade]; }

	Renderer();
	~Renderer();

	bool Initialize(RHI::Device* device);
	void Shutdown();

	// 그림자 패스 + 메인 패스. 프레임의 UI 패스에서는 아래 두 함수 사이에 ImGui 를 그림.
	void Render(const Scene& scene, const Camera& camera, float totalTime, const DebugDraw& debugDraw);
	void BeginUIPass();   // 백버퍼 UNORM 뷰, 깊이 없음, Load (씬 위에 그림)
	void EndUIPass();     // 패스 종료 + 백버퍼 RenderTarget → Present 전이

	// 셰이더를 소스에서 다시 컴파일하고 PSO 캐시를 비움. 실패하면 이전 셰이더를 유지함.
	// Debug 빌드에서는 소스 파일 수정 시각을 감시해 자동으로 호출함. 수동 호출도 가능(F4).
	bool ReloadShaders();

	RenderSettings& GetSettings() { return m_settings; }
	const RenderSettings& GetSettings() const { return m_settings; }
	const Stats& GetStats() const { return m_stats; }
	size_t GetPipelineCount() const;
	size_t GetGpuMeshCount() const { return m_gpuMeshes.size(); }
	size_t GetGpuMaterialCount() const { return m_gpuMaterials.size(); }
	size_t GetTextureCount() const { return m_textures.GetCount(); }
	void ReloadTexture(const std::string& name) { m_textures.Evict(name); }   // 임포트 설정을 바꾼 텍스처를 다음 프레임에 다시 읽게 함
	TextureHandle GetShadowMap() const { return m_shadowMap; }   // 디버그 표시용
	TextureHandle GetCsmMap() const { return m_csmMap; }         // 디버그 표시용 (CSM 전용 아틀라스)

	// 씬 뷰를 오프스크린 텍스처에 그림 (에디터용). 0×0 이면 백버퍼에 직접 그림 (기존 동작).
	// 크기가 바뀌면 텍스처를 다시 만듦. 색 텍스처는 UNORM(TYPELESS) — sRGB 뷰로 그리고 ImGui 는 UNORM SRV 로 읽음.
	void SetSceneTarget(uint32_t width, uint32_t height);
	// HDR: 게임 모드(백버퍼에 직접 그릴 때)의 출력 크기. Engine 이 초기화·리사이즈 때 알려 줌.
	// HDR·깊이 타깃은 최종 출력과 같은 크기여야 하므로 값만 저장하고, 텍스처는 Render 가 만듦 (EnsureGameTarget).
	void SetOutputSize(uint32_t width, uint32_t height);
	bool IsOffscreen() const { return m_mainView.color.IsValid(); }
	TextureHandle GetSceneTexture() const { return m_mainView.color; }
	uint32_t GetSceneWidth() const { return m_sceneWidth; }
	uint32_t GetSceneHeight() const { return m_sceneHeight; }

	// 카메라 미리보기. 에디터가 선택한 카메라 오브젝트의 시점으로 메인 패스를 한 번 더 (같은 그림자 맵, 같은
	// 드로우 목록으로) 작은 텍스처에 그림. 카메라 포인터는 Render 가 끝날 때까지 유효해야 함. nullptr 이거나
	// 0×0 이면 그리지 않음.
	void SetPreviewTarget(uint32_t width, uint32_t height);
	void SetPreviewCamera(const Camera* camera) { m_previewCamera = camera; }
	TextureHandle GetPreviewTexture() const { return m_previewView.color; }

	// 후처리 단계별 썸네일 (에디터의 "Post Stages" 창). 창이 열려 있는 동안 매 프레임 RequestPostCapture 를 부르면
	// 다음 Render 가 단계마다 HDR 결과를 톤매핑한 작은 텍스처에 그림. 요청이 없으면 비용이 없음. 메인 카메라,
	// 에디터(오프스크린) 전용. 단계: 0 Scene (후처리 전), 1 Blur, 2 Bloom (꺼진 효과는 active = false), 3
	// Final (후처리 + 색 효과). 단계 0~2 는 색 효과를 뺀 그림임.
	static constexpr uint32_t kPostStageCount = 4;
	struct PostStageThumb
	{
		TextureHandle texture;
		const char* name = "";
		bool active = false;   // 이번 프레임에 그려졌는가
	};
	void RequestPostCapture();   // 썸네일 텍스처를 (필요하면) 만듦 — UI 를 만드는 중에 불러야 함. 이번 프레임의 UI 가 그 텍스처 ID 를 씀
	const PostStageThumb& GetPostStage(uint32_t index) const { return m_stageThumbs[index]; }
	uint32_t GetPostStageWidth() const { return m_thumbWidth; }
	uint32_t GetPostStageHeight() const { return m_thumbHeight; }

	// 메시/재질이 파괴되거나 내용이 바뀌면 캐시 항목을 버림.
	void InvalidateMesh(const Mesh* mesh);
	void InvalidateMaterial(const Material* material);
	// 씬의 모든 메시·재질 캐시를 버림 (Scene::Clear 전에 호출). releaseTextures 가 true 면 내장이 아닌 텍스처도 버림.
	void InvalidateScene(const Scene& scene, bool releaseTextures);

private:
	struct GpuMesh
	{
		BufferHandle vertexBuffer;
		BufferHandle indexBuffer;
		uint32_t indexCount = 0;
		Format indexFormat = Format::R32_UINT;   // 정점 65,536개 미만이면 R16_UINT
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
		TextureHandle boundMetallicRoughness;   // PBR
		TextureHandle boundOcclusion;
		TextureHandle boundEmissive;
		SamplerHandle boundSampler;
		uint64_t uploadedFrame = UINT64_MAX;   // 이번 프레임에 상수를 올렸는지

		// 셋을 만들 때 본 입력의 사본. 프레임마다 이것과 재질을 비교해, 같으면 텍스처 이름 → 핸들 조회를
		// 통째로 건너뜀 (바뀌는 것은 재질 편집·샘플러 덮어쓰기·텍스처 캐시 비우기뿐).
		std::string albedoName, normalName, metallicRoughnessName, occlusionName, emissiveName;
		bool albedoSrgb = true;
		SamplerHandle sampler;
		uint64_t textureGeneration = UINT64_MAX;   // 셋을 만들 때의 TextureCache::GetGeneration()
	};

	// 이번 프레임에 그릴 오브젝트 하나의 계산 결과. 월드 행렬(부모 계층 곱)·역행렬·경계 구를 프레임에 한 번만
	// 구해 두고, 그림자 뷰 여러 개 + 메인 + 미리보기 패스의 모든 드로우가 이것을 다시 씀.
	struct ObjectData
	{
		DirectX::XMFLOAT4X4 world;              // 전치해서 저장 (b1 에 그대로 올림)
		DirectX::XMFLOAT4X4 worldInvTranspose;  // 올리는 값은 W⁻¹ (ShaderConstants.h)
		DirectX::XMFLOAT4 sphere;               // 월드 경계 구 (xyz = 중심, w = 반지름). 컬링용
		const std::vector<DirectX::XMFLOAT4X4>* skinPalette = nullptr;   // 스키닝 오브젝트의 조인트 팔레트 (씬이 소유, 이번 프레임 동안 유효). 비면 단위행렬
		bool skinned = false;                   // 스키닝 메시면 드로우마다 b7 에 팔레트를 올림
	};

	struct DrawItem
	{
		PipelineHandle pipeline;         // 메인 패스 PSO (정렬 키)
		PipelineHandle shadowPipeline;   // 그림자 패스 PSO. unlit 재질은 빈 핸들 (그림자를 만들지 않음)
		const Material* material;        // 정렬 키
		const Mesh* mesh;                // 정렬 키
		const GpuMaterial* gpuMaterial;  // BuildDrawList 가 만들어 둠 (드로우 루프에서는 생성하지 않음)
		const GpuMesh* gpuMesh;
		uint32_t object = 0;             // m_objects 인덱스
		uint32_t indexStart = 0;         // 서브메시 범위
		uint32_t indexCount = 0;
	};

	// 뷰 하나(메인 씬 뷰, 카메라 미리보기)가 그리는 데 필요한 텍스처 묶음. 메인과 미리보기가 같은 함수로
	// 만들어지고 그려짐. G-buffer·SSAO 텍스처를 추가할 때 여기에 넣으면 두 뷰가 함께 받음.
	struct PostScratch;
	struct ViewTarget;

	// ---- 셰이더·레이아웃·패스 표 (Renderer.cpp 의 kShaders, kPasses) ----
	// 셰이더를 추가할 때: ShaderId 에 한 줄, kShaders 에 한 줄. 로드·핫리로드 감시·검증·해제는 표를 돌며 함.
	enum ShaderId : uint32_t { Shader_BasicVS, Shader_BasicPS, Shader_ShadowVS, Shader_SkyVS, Shader_SkyPS, Shader_ScreenVS, Shader_TonemapPS, Shader_PostPS, Shader_BasicSkinVS, Shader_ShadowSkinVS, Shader_DebugLineVS, Shader_DebugLinePS, Shader_Count };
	enum LayoutId : uint32_t { Layout_Frame, Layout_Object, Layout_Material, Layout_Post, Layout_Count };
	// 패스 = (VS, PS, 레이아웃 목록). PSO Desc 의 셰이더·bindingLayouts 가 이 표에서 나오고, 바인딩 대조도 패스
	// 단위로 함 (그 패스의 레이아웃에 선언된 슬롯만 통과).
	// Pass_MainSkin / Pass_ShadowSkin 은 스키닝 메시용 (정점 포맷 PositionColorNormalTexcoordTangentSkin). 스킨 VS 만 다르고 PS·레이아웃은 같음.
	enum PassId : uint32_t { Pass_Main, Pass_Shadow, Pass_Sky, Pass_Tonemap, Pass_Post, Pass_MainSkin, Pass_ShadowSkin, Pass_DebugLine, Pass_Count };
	// 셋 인덱스 = 그 패스의 PipelineStateDesc::bindingLayouts 인덱스 (CommandList::SetResourceSet 의 첫 인자).
	// 메인 { 프레임, 오브젝트, 재질 }, 그림자 { 프레임, 오브젝트 }, 스카이 { 프레임 }, 후처리 { 후처리 }.
	static constexpr uint32_t kSetFrame = 0;
	static constexpr uint32_t kSetObject = 1;
	static constexpr uint32_t kSetMaterial = 2;
	static constexpr uint32_t kSetPost = 0;
	struct ShaderInfo;   // 파일·진입점·스테이지·이름
	struct PassInfo;     // 이름·VS·PS·레이아웃 목록
	static const ShaderInfo kShaders[Shader_Count];
	static const PassInfo kPasses[Pass_Count];

	// 표(kShaders)의 셰이더를 전부 읽거나 컴파일하고, cbuffer 크기와 패스별 바인딩을 대조한 뒤 핸들을 교체함.
	bool LoadShaders(bool fromSourceOnly);
	// 패스마다: 그 패스의 VS/PS 가 실제로 쓰는 슬롯이 그 패스의 레이아웃에 (그 스테이지로) 선언됐는지 대조함.
	// 한 패스의 레이아웃끼리 레지스터가 겹치는지도 봄. code[i] = ShaderId i 의 바이트코드.
	bool ValidateBindings(const std::vector<uint8_t>* code) const;
	// 표와 현재 셰이더 핸들로 m_passDesc 를 다시 채움 (초기화, 핫리로드 뒤). PSO 핸들 캐시도 비움.
	void BuildPassDescs();
	void CheckHotReload(float totalTime);
	bool CreateSamplers();
	bool CreateLayouts();
	bool CreateShadowResources();

	// 그릴 것을 모음: 오브젝트마다 월드 행렬·경계 구를 한 번 계산하고(m_objects), 서브메시마다 DrawItem 을 만듦.
	// GPU 메시·재질(텍스처 포함)도 여기서 만들어 둠 — Render 가 이 구간을 업로드 묶음으로 감쌈.
	void BuildDrawList(const Scene& scene);
	void UploadPerFrameConstants(const Camera& camera, float totalTime);   // b0: view/proj 는 이 카메라 기준, 나머지는 프레임 공통 값
	void RenderShadowPass(RHI::CommandList& cmd);
	void RenderShadowAtlasPass(RHI::CommandList& cmd, uint32_t atlas);   // atlas 0 = 메인(m_shadowMap), 1 = CSM(m_csmMap). 그 아틀라스의 뷰만 그림
	// 뷰 하나의 씬 패스 (메인·미리보기 공통): view.hdr + view.depth 에 불투명 드로우와 스카이박스를 그림.
	// b0 는 호출 전에 그 카메라로 올려 둘 것. 끝나면 view.hdr 는 ShaderResource 상태.
	void RenderScenePass(RHI::CommandList& cmd, const Scene& scene, ViewTarget& view, const Camera& camera, const char* debugName);
	void DrawItems(RHI::CommandList& cmd, const Scene& scene, const Camera& camera);   // 씬 패스의 드로우 루프 (절두체 컬링, 끝에 스카이박스)
	// 디버그 선 (본·Raycast 등). 선은 Engine 이 소유한 DebugDraw 가 모으고, Renderer 는 읽기만 함: UploadDebugLines 가 그 월드 공간
	// 선 목록을 프레임마다 동적 정점 버퍼에 올리고, DrawDebugLines 가 씬 패스 끝(하늘 뒤)에 깊이 테스트 없이 그림.
	void UploadDebugLines(const DebugDraw& debugDraw);
	void DrawDebugLines(RHI::CommandList& cmd);
	PipelineHandle GetDebugLinePipeline();
	void DrawSkybox(RHI::CommandList& cmd);                               // 전체 화면 삼각형 하나 (Draw(3), 정점 버퍼 없음)
	// HDR: 씬 패스가 그린 HDR 텍스처(hdrSet 의 t13)를 읽어 노출·ACES 를 적용하고 output 에 sRGB 로 씀.
	// output 은 RenderTarget 으로 전이했다가 끝나면 outputAfter 로 둠. HDR 텍스처는 이미 ShaderResource
	// 상태여야 함. srcW/srcH = HDR 텍스처 크기, dstW/dstH = output 크기 (썸네일은 더 작음).
	// skipGrade 가 참이면 색 효과(GreyScale 등)를 건너뜀.
	void RenderTonemapPass(RHI::CommandList& cmd, ResourceSetHandle hdrSet, TextureHandle output,
		ResourceState outputAfter, const char* debugName,
		uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH, bool skipGrade = false);
	// 풀스크린 후처리 패스 하나 (톤매핑·블러·블룸 공통). 출력의 상태 전이(→ RenderTarget → after),
	// 패스 시작·끝, Draw(3), 통계까지 함. 입력 셋(t13)이 가리키는 텍스처는 이미 ShaderResource 상태여야 하고,
	// PostParams(b4)는 호출 전에 UploadPostParams 로 올려 둘 것. 텍스처의 현재 상태는 RHI 가 추적하므로
	// "지금 무슨 상태인지"는 넘기지 않음.
	struct PostPassDesc
	{
		PipelineHandle pipeline;
		ResourceSetHandle input;
		TextureHandle output;
		ResourceState outputAfter = ResourceState::ShaderResource;
		LoadOp load = LoadOp::DontCare;   // 덧그리는 패스(블룸 합성)는 Load
		bool srgbView = false;
		const char* debugName = "";
	};
	void RenderPostPass(RHI::CommandList& cmd, const PostPassDesc& desc);

	// HDR 구간 후처리 (RendererPost.cpp). 타깃(메인·카메라 미리보기)마다 스크래치 텍스처를 하나씩 가짐.
	// 효과는 hdr 텍스처 안에서 "제자리"로 끝남 — 마지막 패스가 결과를 hdr 에 되돌려 쓰므로 톤 패스의
	// 입력(ViewTarget::hdrSet)은 그대로임. 스크래치는 효과가 켜질 때 처음 만들고, 타깃 크기가 바뀌면 다시 만듦.
	struct PostScratch
	{
		static constexpr uint32_t kBloomLevels = 5;
		struct BloomLevel
		{
			TextureHandle texture;                    // 레벨 i 는 hdr 의 1/2^(i+1) 크기
			ResourceSetHandle set;                    // Layout_Post: t13 = texture
			uint32_t width = 0;
			uint32_t height = 0;
		};
		uint32_t width = 0;                           // 이 스크래치가 맞춘 hdr 크기. 바뀌면 전부 다시 만듦
		uint32_t height = 0;
		TextureHandle temp;                           // 블러 가로 패스의 결과 (hdr 와 같은 크기·포맷). 블러가 처음 켜질 때 만듦
		ResourceSetHandle tempSet;                    // Layout_Post: t13 = temp
		BloomLevel bloom[kBloomLevels];               // 블룸 축소 체인. 블룸이 처음 켜질 때 만듦
		uint32_t bloomCount = 0;
	};
	struct ViewTarget
	{
		TextureHandle color;       // 최종 출력 RGBA8 (TYPELESS: sRGB RTV + UNORM SRV, ImGui 가 읽음). 게임 모드의 메인 뷰는 비어 있음 — 백버퍼에 씀
		TextureHandle depth;       // D24S8. DepthStencil | ShaderResource 라 후처리가 SRV 로 읽을 수 있음
		TextureHandle hdr;         // R16G16B16A16_FLOAT. 씬 패스의 렌더 타깃
		ResourceSetHandle hdrSet;  // Layout_Post: t13 = hdr (톤 패스·후처리의 입력)
		PostScratch post;          // 블러·블룸 스크래치 (hdr 크기를 따름)
		uint32_t width = 0;
		uint32_t height = 0;
		bool IsValid() const { return depth.IsValid() && hdr.IsValid() && hdrSet.IsValid(); }
	};
	// withColor: 최종 출력 텍스처도 만듦 (에디터의 씬 뷰·미리보기). 거짓이면 색은 백버퍼가 맡음 (게임 모드).
	// 실패하면 만든 것을 모두 지우고 false.
	bool CreateViewTarget(ViewTarget& view, uint32_t width, uint32_t height, bool withColor, const char* name);
	void DestroyViewTarget(ViewTarget& view);
	// 게임 모드(오프스크린이 아님): 출력 크기(SetOutputSize)에 맞는 메인 뷰 타깃을 보장함.
	void EnsureGameTarget();
	// 필요한 것만 만듦 (needBlur → temp, needBloom → 축소 체인). 크기가 바뀌었으면 전부 버리고 다시 만듦.
	// 실패하면 false.
	bool EnsurePostScratch(PostScratch& scratch, uint32_t width, uint32_t height, bool needBlur, bool needBloom);
	void DestroyPostScratch(PostScratch& scratch);
	// view.hdr 가 ShaderResource 상태일 때 부르고, 끝나도 ShaderResource 상태임. 켜진 효과가 없으면 아무것도 안 함.
	// capture 가 참이면 단계마다 썸네일을 그림 (CapturePostStage).
	void ApplyHdrEffects(RHI::CommandList& cmd, ViewTarget& view, bool capture);
	void EnsureStageThumbs(uint32_t hdrWidth, uint32_t hdrHeight);
	void DestroyStageThumbs();
	void CapturePostStage(RHI::CommandList& cmd, uint32_t stage, const char* name, ResourceSetHandle hdrSet, uint32_t hdrWidth, uint32_t hdrHeight, bool skipGrade);
	PipelineHandle GetPostPipeline(bool additive);   // FP16 출력, 깊이 없음. additive = One/One 블렌드 (블룸 합성)
	void UploadPostParams(const PostParamsConstants& params);
	ResourceSetHandle CreatePostSet(TextureHandle input, const char* debugName);   // Layout_Post 순서: b0, b4, t13, s6
	// 이번 프레임의 환경 바인딩. Scene::environment 의 이름들을 ResolveEnvironment 가 핸들과 셰이더 파라미터로 바꿈.
	struct EnvironmentBinding
	{
		TextureHandle skybox;       // t9  (없으면 검은 큐브)
		TextureHandle irradiance;   // t10 (IBL 확산 조도. 없으면 스카이박스 → 반사 큐브 → 검은 큐브 순으로 대체)
		TextureHandle specular;     // t11 (IBL 반사 프리필터. 없으면 스카이박스 → 검은 큐브)
		TextureHandle brdfLut;      // t12 (없으면 흰색 2D, 셰이더는 해석적 근사를 씀)
		uint32_t specularMips = 1;  // t11 의 밉 수 — 거칠기 → 밉 매핑의 끝
		float irradianceLod = 0.0f; // t10 을 읽을 밉. 진짜 조도 큐브면 0, 스카이박스로 대체하면 끝에서 두 번째 밉(면당 2×2)
		bool hasEnvironment = false;   // 거짓이면 셰이더가 ambientColor 를 씀
		bool hasBrdfLut = false;
		float iblIntensity = 1.0f;
		float exposure = 1.0f;
		bool toneMapping = false;

		bool SameTextures(const EnvironmentBinding& other) const
		{
			return skybox == other.skybox && irradiance == other.irradiance && specular == other.specular && brdfLut == other.brdfLut;
		}
	};
	void ResolveEnvironment(const SceneEnvironment& environment);   // → m_environment
	// 프레임 셋(b0, b2, t8, s4, t9, s5, t10, t11, t12, b5, b6, t14, t15)을 m_environment 의 텍스처로 만듦.
	// 꽂힌 텍스처가 바뀔 때만 다시 만듦 (ResourceSet 은 값 객체).
	bool UpdateFrameSet();
	void UploadObjectConstants(uint32_t object);   // b1: m_objects[object] 의 미리 계산한 행렬을 올림
	DirectX::XMMATRIX ComputeLightViewProj(const GpuLight& light) const;
	// 스팟: 원뿔을 담는 정사각 원근 절두체. outTanHalfFov = tan(시야각 / 2) (텍셀의 월드 크기 계수를 구할 때 씀)
	DirectX::XMMATRIX ComputeSpotViewProj(const GpuLight& light, float& outTanHalfFov) const;
	// 포인트: 큐브 6면 중 face 번째 (+X, −X, +Y, −Y, +Z, −Z — D3D 큐브맵 면 순서). 시야각은 PCF 여백만큼 90°
	// 보다 조금 넓음.
	DirectX::XMMATRIX ComputePointFaceViewProj(const GpuLight& light, uint32_t face) const;

	// 다중 광원 그림자. lights[i].shadowIndex 를 채우고 m_shadowViews 를 만듦 (Render 가
	// UploadPerFrameConstants 보다 먼저 부름).
	void BuildShadowViews(const Camera& camera, std::vector<GpuLight>& lights);
	// 씬의 조명을 GPU 배치로 옮김 (방향 정규화, shadowIndex = −1) → m_gpuLights
	void BuildGpuLights(const Scene& scene);
	// 조명 구조체 버퍼(t15)와 b2 를 올림. 버퍼가 모자라면 두 배로 다시 만들고 프레임 셋을 다시 만들게 함.
	bool UploadLights(const Scene& scene);
	// 방향광 CSM: 카메라 절두체를 N 조각으로 나눠 조각마다 직교 뷰를 만듦 (경계 구 + 텍셀 스냅). 첫
	// 캐스케이드의 뷰 번호를 돌려줌 (실패 시 UINT32_MAX).
	uint32_t AddCascadeViews(const Camera& camera, const GpuLight& light, uint32_t lightIndex);
	// 새 뷰의 번호를 돌려줌. 자리가 없으면 UINT32_MAX.
	uint32_t AddShadowView(const DirectX::XMMATRIX& viewProj, uint32_t x, uint32_t y, uint32_t size,
		const DirectX::XMFLOAT4& params, uint32_t lightIndex, uint32_t atlas = 0);
	void UploadShadowConstants();   // b6 전체 + b5 단위 행렬. 그림자가 꺼진 프레임에도 부름 (D3D12 의 Dynamic CB 갱신 검사)

	const GpuMesh* GetOrCreateGpuMesh(const Mesh& mesh);
	const GpuMaterial* GetOrCreateGpuMaterial(const Material& material, const Scene* scene);
	// mirrored: 월드 행렬의 행렬식이 음수(반사)인 오브젝트. 감기 순서가 뒤집히므로 앞면
	// 정의(frontCounterClockwise)를 반대로 둠.
	// PSO 핸들은 "Desc 를 가르는 몇 비트"를 키로 여기서 한 번 더 캐시함 — 드로우마다 Desc 전체를 복사·해시해
	// Device 캐시를 찾지 않음. 셰이더 핫리로드(BuildPassDescs)가 비움.
	PipelineHandle GetPipelineFor(const Material& material, VertexFormat vertexFormat, bool mirrored = false);
	PipelineHandle GetShadowPipelineFor(const Material& material, VertexFormat vertexFormat, bool mirrored = false);
	PipelineHandle GetSkyPipeline();   // 정점 속성 0개, 깊이 LessEqual + 쓰기 끔, 컬링 없음
	PipelineHandle GetTonemapPipeline();
	SamplerHandle GetSampler(SamplerPreset preset) const;

private:
	RHI::Device* m_device = nullptr;

	ShaderHandle m_shaders[Shader_Count];
	BufferHandle m_perFrameCB;
	BufferHandle m_perObjectCB;
	BufferHandle m_skinCB;                       // b7 Skin (스키닝 조인트 팔레트). 스키닝 드로우마다 올림
	std::vector<DirectX::XMFLOAT4X4> m_skinScratch;   // 전치한 팔레트를 만드는 임시 배열 (kMaxSkinJoints)
	BufferHandle m_lightCB;        // b2 Lights (주변광, 조명 수)
	BufferHandle m_lightBuffer;    // t15 조명 구조체 버퍼 (GpuLight × m_lightCapacity). Dynamic
	uint32_t m_lightCapacity = 0;  // 원소 수. 모자라면 두 배로 다시 만듦
	std::vector<GpuLight> m_gpuLights;   // 이번 프레임에 올릴 조명 (BuildGpuLights)
	BufferHandle m_postParamsCB;   // b4 PostParams
	BufferHandle m_shadowPassCB;   // b5 ShadowPass (뷰마다 올림)
	BufferHandle m_shadowsCB;      // b6 Shadows (프레임에 한 번)

	// 갱신 빈도별 레이아웃 (LayoutId 순서). 선언 사본(m_layoutDescs)은 패스별 바인딩 대조가 씀.
	//   Layout_Frame     b0 (VS|PS), b2 (PS), t8 그림자 맵, s4 비교 샘플러, t9 스카이박스, s5, t10~t12 IBL (모두 PS), b5 그림자 패스 행렬 (VS), b6 그림자 뷰 배열 (PS), t14 CSM, t15 조명 구조체 버퍼
	//   Layout_Object    b1 (VS)
	//   Layout_Material  b3 (VS|PS), t0 알베도, t1 노멀, t2 금속성/거칠기, t3 AO, t4 발광, s0 (모두 PS)
	//   Layout_Post      b0, b4, t13, s6 (모두 PS)
	BindingLayoutHandle m_layouts[Layout_Count];
	BindingLayoutDesc m_layoutDescs[Layout_Count];
	ResourceSetHandle m_frameSet;
	ResourceSetHandle m_objectSet;

	// 패스별 PSO Desc 의 고정 부분 (BuildPassDescs 가 표에서 채움). 설정·재질에 따라 달라지는 필드는 Get*Pipeline 이 덮어씀.
	//   Pass_Main     셰이더·레이아웃 { 프레임, 오브젝트, 재질 }
	//   Pass_Shadow   VS만, RTV 0개, DSV D32_FLOAT, 깊이 바이어스
	//   Pass_Sky      프레임 레이아웃 하나만 씀
	//   Pass_Tonemap  풀스크린 삼각형, 깊이 없음
	//   Pass_Post     블러·블룸 (톤 패스와 같은 모양, PS 만 다름). RTV 는 FP16
	PipelineStateDesc m_passDesc[Pass_Count];
	PipelineStateDesc m_postAddDesc;  // Pass_Post + 가산 블렌드
	// PSO 핸들 캐시 (작은 키 → 핸들). 키는 Get*PipelineFor 가 만듦.
	std::unordered_map<uint32_t, PipelineHandle> m_mainPipelines;
	std::unordered_map<uint32_t, PipelineHandle> m_shadowPipelines;
	PipelineHandle m_skyPipeline;
	PipelineHandle m_debugLinePipeline;
	BufferHandle m_debugLineVB;                       // 동적 정점 버퍼 (DebugDraw::kMaxVertices 개)
	uint32_t m_debugLineCount = 0;                    // 올린 정점 수 (0 이면 그리지 않음)
	PipelineHandle m_tonemapPipelines[2];   // [srgbOutput]
	PipelineHandle m_postPipelines[2];      // [additive]
	RenderSettings m_settings;
	Stats m_stats;
	uint64_t m_frameNumber = 0;

	Material m_defaultMaterial;     // GameObject.material == nullptr 일 때

	// 샘플러 프리셋과 텍스처 캐시 (2D·큐브, 내장 흰색·평평한 노멀·검은 큐브)
	static constexpr uint32_t kSamplerPresetCount = 5;
	SamplerHandle m_samplers[kSamplerPresetCount];
	TextureCache m_textures;

	EnvironmentBinding m_environment;   // 이번 프레임 (ResolveEnvironment)
	PostProcessSettings m_postSettings;   // 이번 프레임 (ResolveEnvironment 가 씬의 환경에서 복사)
	ShadowSettings m_shadowSettings;      // 이번 프레임 (Render 가 맨 앞에서 씬의 환경에서 복사. 그림자 행렬과 b0 가 씀)
	EnvironmentBinding m_boundEnvironment;   // 지금 m_frameSet 에 꽂힌 것. 비우면 다음 UpdateFrameSet 이 셋을 다시 만듦

	// 그림자 맵 (D32_FLOAT, DSV + SRV) 과 비교 샘플러. 상태는 Barrier 로 전이함 (현재 상태는 RHI 가 추적).
	TextureHandle m_shadowMap;
	SamplerHandle m_shadowSampler;
	TextureHandle m_csmMap;                // 방향광 CSM 전용 아틀라스 (kCsmAtlasWidth × kCsmAtlasHeight, 같은 비교 샘플러)

	// 뷰 타깃. 메인: 에디터면 씬 뷰 크기(SetSceneTarget, color 있음), 게임 모드면 출력 크기(EnsureGameTarget,
	// color 없음 — 톤 패스가 백버퍼에 씀). 미리보기: 선택한 카메라 오브젝트의 시점 (SetPreviewTarget).
	ViewTarget m_mainView;
	ViewTarget m_previewView;
	uint32_t m_sceneWidth = 0;     // SetSceneTarget 이 요청한 씬 뷰 크기 (0 = 오프스크린 아님)
	uint32_t m_sceneHeight = 0;
	uint32_t m_outputWidth = 0;    // 게임 모드 출력(백버퍼) 크기 — SetOutputSize
	uint32_t m_outputHeight = 0;
	PostStageThumb m_stageThumbs[kPostStageCount];   // 단계별 썸네일 (RGBA8, 폭 384). 요청이 있을 때 처음 만듦
	uint32_t m_thumbWidth = 0;
	uint32_t m_thumbHeight = 0;
	bool m_postCaptureRequested = false;   // 창이 이번 프레임에 요청함 → 다음 Render 가 읽고 지움
	bool m_postCapture = false;            // 이번 Render 가 캡처함
	const Camera* m_previewCamera = nullptr;
	bool m_shadowEnabledThisFrame = false;
	ShadowView m_shadowViews[MAX_SHADOW_VIEWS];
	uint32_t m_shadowViewCount = 0;
	uint32_t m_skippedShadowLights = 0;   // castShadows 인데 자리가 없어 그림자를 못 받은 조명 수
	uint32_t m_usedLocalTiles = 0;        // 이번 프레임에 쓴 지역광 타일 수 (0..kShadowLocalTileCount)
	// CSM (이번 프레임). 그림자는 메인 카메라 기준으로 한 번만 만들어서, 미리보기 패스에서도 PS 가 이 값으로
	// 캐스케이드를 고름.
	uint32_t m_cascadeCount = 0;          // 0 = CSM 아님
	DirectX::XMFLOAT4 m_cascadeSplits = {};   // 캐스케이드 i 가 끝나는 카메라 전방 거리 (없는 칸은 0)
	DirectX::XMFLOAT3 m_cascadeOrigin = {};   // 캐스케이드를 계산한 카메라의 위치·전방
	DirectX::XMFLOAT3 m_cascadeForward = {};
	DirectX::XMFLOAT3 m_cascadeCorners[4][8] = {};   // 에디터 오버레이용 (GetCascadeFrustumCorners)
	bool m_cascadeFrozen = false;
	Camera m_cascadeFreezeCamera;                    // 고정 중일 때 캐스케이드를 계산하는 카메라 사본

	// Scene이 메시(shared_ptr)·재질(unique_ptr)을 힙에 따로 두므로 포인터가 안정적임. 그래서 키로 씀.
	std::unordered_map<const Mesh*, GpuMesh> m_gpuMeshes;
	std::unordered_map<const Material*, GpuMaterial> m_gpuMaterials;
	std::vector<DrawItem> m_drawList;
	std::vector<ObjectData> m_objects;   // 이번 프레임에 그릴 오브젝트 (DrawItem::object 가 가리킴). BuildDrawList 가 채움

	// 핫리로드: 감시하는 소스 파일과 마지막 수정 시각
	struct WatchedFile
	{
		std::wstring path;
		uint64_t writeTime = 0;
	};
	std::vector<WatchedFile> m_watched;
	float m_nextWatchTime = 0.0f;
};

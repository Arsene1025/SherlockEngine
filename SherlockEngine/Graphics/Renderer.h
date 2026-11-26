#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <DirectXMath.h>
#include "RHI/Handle.h"
#include "RHI/GraphicsConfig.h"   // kFrameCount (GPU 클러스터 통계 리드백 슬롯)
#include "RHI/PipelineTypes.h"
#include "RHI/RenderPassTypes.h"
#include "Graphics/VertexTypes.h"   // VertexFormat
#include "Graphics/TextureCache.h"
#include "RHI/BindingTypes.h"   // BindingLayoutDesc (레이아웃 선언 사본)
#include "Scene/Material.h"
#include "Scene/Camera.h"   // 컬링 고정용 카메라 사본 (m_cullFreezeCamera)
#include "Scene/Light.h"
#include "Scene/Ocean.h"   // OceanWave (m_frameOcean)
#include "Scene/PostProcessSettings.h"
#include "Scene/ShadowSettings.h"
#include "Scene/SsaoSettings.h"

struct UIDrawList;
struct UIAtlasImage;

namespace RHI
{
	class Device;
	class CommandList;
}
class Scene;
class DebugDraw;
struct SceneEnvironment;
class Mesh;
class GameObject;
class ParticleEmitter;
struct PostParamsConstants;   // ShaderConstants.h
struct GpuLight;              // ShaderConstants.h (조명 하나의 GPU 배치)

// ImGui에서 바꾸는 렌더 설정. Material의 옵션과 합쳐져 PipelineStateDesc가 됨.
struct RenderSettings
{
	bool wireframe = false;
	bool cullBack = true;
	bool srgbOutput = true;     // 최종 출력(톤 패스의 RTV)을 sRGB 뷰로 씀. 끄면 감마 보정 없이 출력함 (비교용). 씬 패스는 FP16 선형이라 무관함
	int samplerOverride = -1;   // -1 = 재질 설정대로. 그 외 SamplerPreset 값으로 전부 덮어씀
	// 가우시안 블러를 컴퓨트 셰이더로 (BlurComputeShader.hlsl). 끄면 풀스크린 PS (PostPixelShader 모드 1). 결과는
	// 같아야 함 — 두 경로의 스크린샷 비교가 컴퓨트 RHI 의 검증임 (D3D12 0 픽셀, D3D11 1 픽셀 최하위 비트 차이).
	// 기본은 컴퓨트. 컴퓨트 PSO 를 만들 수 없으면 (미지원 백엔드) 켜져 있어도 PS 로 감.
	// 씬 파일에 저장하지 않음 (에디터 Render Settings · --compute-blur=0 으로 PS).
	bool computeBlur = true;
	// 파티클 시뮬레이션 시간. 0 = 실제 경과 시간 (프레임 사이 시간, 0.1 초에서 자름). 0 보다 크면 프레임마다 이 값만큼
	// 진행 — 자동 검증(--particle-step=<초>)에서 스크린샷이 실행마다 같아지도록. 씬 파일에 저장하지 않음.
	float particleFixedStep = 0.0f;
	// 알파 이미터를 뷰마다 정렬함. 끄면 생존 목록 순서로 그림 — 정렬 효과를 비교하는 검증용 (--particle-sort=0).
	bool particleSort = true;

	// 그림자 (켜고 끄는 스위치. 조명마다는 Light::castShadows) 와 노멀 매핑. 그림자 값(바이어스·세기·노말
	// 오프셋·직교 크기·거리)은 씬 파일에 들어가도록 SceneEnvironment::shadow 에 둠. 여기에는 켜고 끄는
	// 스위치만 있음.
	bool shadows = true;
	bool normalMapping = true;

	// ---- 2·3단계 (docs/plan/deferred-clustered-ssao-record.html). 씬 파일에 저장하지 않음 — 렌더러 경로 스위치 ----
	// 디퍼드: 불투명 재질을 G-buffer 에 쓰고 풀스크린 패스 한 번으로 조명함. 끄면 전부 포워드 (기존 경로, 정답 화면).
	// 켜져 있어도 G-buffer 에 못 담는 재질(unlit·블렌딩·투과·코팅 노멀 맵)과 디버그 뷰 5(UV)는 포워드로 그림.
	// 에디터 Render Settings · --deferred=1|0.
	bool deferred = true;
	// 클러스터드 라이팅: 지역광을 화면 타일 × 깊이 슬라이스 상자(16 × 9 × 24)에 나눠 담고, 픽셀은 자기 상자의 목록만
	// 돎. 끄면 픽셀마다 조명 전부 (ShadeAllLights). 포워드·디퍼드 둘 다에 적용. --clustered=1|0.
	bool clustered = true;
	// 클러스터 비닝을 컴퓨트 셰이더로 (타일 → 상자 2단계, docs/plan/gpu-cluster-binning-record.html). 끄면 CPU 비닝 (BuildClusters) —
	// 결과 비교용. 컴퓨트 파이프라인을 못 만들면 CPU 로 돌아감. 에디터 Render Settings · --cluster-gpu=1|0.
	bool clusterGpu = true;
	// GPU 비닝의 1단계(화면 타일). 끄면 상자 3456 개가 조명 전체를 직접 검사 (2단계 비닝의 효과 측정용). --cluster-tiles=1|0.
	bool clusterGpuTiles = true;
	// 3a: 그림자 타일을 "화면에 보이는 조명" 중 점수 높은 순으로 줌. 끄면 예전처럼 씬 순서대로 앞에서부터. --shadow-priority=1|0.
	bool shadowPriority = true;
	// SSAO 켜짐 덮어쓰기: −1 = 씬 파일 값 (environment.ssao.enabled), 0 = 끔, 1 = 켬. --ssao= 실행 인자가 씀 (저장하지 않음).
	int ssaoOverride = -1;
	// 모션 블러 켜짐 덮어쓰기: −1 = 씬 파일 값 (environment.post.motionBlurEnabled), 0 = 끔, 1 = 켬. --motion-blur= (저장하지 않음).
	int motionBlurOverride = -1;
	// 모션 블러 기준 fps 덮어쓰기 (−1 = 씬 값, 0 = 맞추지 않음). --motion-blur-fps= (저장하지 않음).
	float motionBlurFpsOverride = -1.0f;
	// 모션 블러 길이 보정에 쓰는 프레임 시간 덮어쓰기 (초, 0 = 실제 값). 자동 검증이 실행 속도와 무관한 결과를 얻도록 --frame-dt=.
	// 에디터 재생의 scene.Update dt 도 이 값을 씀 (EditorApp::OnUpdate).
	float frameDeltaOverride = 0.0f;
	// TAA 켜짐 덮어쓰기 (−1 = 씬 값, --taa=) 와 지터 (끄면 TAA 가 같은 위치를 계속 섞음 — 배관 검증용, --taa-jitter=0).
	int taaOverride = -1;
	bool taaJitter = true;
	// 바다 파도 시간 고정 (초, 음수 = 실제 시간). 스크린샷이 실행마다 같아지고 바다 속도가 카메라 속도만 남음 (뷰 17 검정) — --ocean-time=.
	float oceanTimeOverride = -1.0f;

	// 디버그 뷰 (0 조명, 1 알베도, 2 월드 노멀, 3 깊이, 4 그림자 계수, 5 UV, 6 금속성/거칠기, 7 AO, 8 캐스케이드, 9 F0,
	// 10 클리어코트, 11 월드 위치, 12 기하 노멀, 13 SSAO, 14 클러스터 조명 수, 15 클러스터 슬라이스, 16 속도, 17 속도 오차) 와 깊이 뷰의 범위(월드 단위)
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
// 구현은 파일 일곱으로 나뉨:
//   Renderer.cpp         초기화, 셰이더 표, 프레임 흐름(Render), 드로우 목록·컬링, GPU 캐시, PSO
//   RendererShadow.cpp   그림자 뷰 계산(방향광·CSM·스팟·포인트), 아틀라스 패스
//   RendererTargets.cpp  뷰 타깃(메인·카메라 미리보기) 생성·해제, UI 패스
//   RendererPost.cpp     HDR 구간 후처리(블러·블룸), 톤 패스, 단계별 썸네일
//   RendererDeferred.cpp 디퍼드: G-buffer 생성, G-buffer 패스 → 라이팅 패스 → 포워드 패스, 관련 PSO (2단계)
//   RendererClusters.cpp 조명 경계 구, 메인 카메라 컬링·그림자 우선순위 (3a), 클러스터 비닝·업로드 (3b)
//   RendererSsao.cpp     SSAO 타깃·커널·패스 (5단계)
//   (설계·읽는 순서: docs/plan/deferred-clustered-ssao-record.html)
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
		uint32_t triangles = 0;   // 메인 패스 삼각형 수
		uint32_t pipelineSwitches = 0;
		uint32_t resourceSetSwitches = 0;
		uint32_t meshSwitches = 0;
		uint32_t renderPasses = 0;
		uint32_t barriers = 0;
		uint32_t shaderReloads = 0;      // 누적
		uint32_t shadowViews = 0;        // 이번 프레임에 그린 그림자 뷰(아틀라스 타일) 수
		uint32_t shadowCulled = 0;       // 그 뷰의 절두체나 지역광 범위 밖이라 그림자 패스에서 건너뛴 (뷰, 드로우) 쌍 수
		uint32_t culled = 0;             // 카메라 절두체 밖이라 메인·미리보기 패스에서 건너뛴 드로우 수
		uint32_t drawItems = 0;          // 이번 프레임 드로우 목록 크기 (서브메시 단위, 컬링 전). draws·culled 는 미리보기 뷰와 풀스크린·파티클·UI 드로우까지 세므로 draws + culled 와 같지 않음
		uint32_t particleEmitters = 0;   // 이번 프레임에 시뮬레이션한 이미터 수
		uint32_t particleCapacity = 0;   // 그 이미터들의 버퍼 원소 합 (용량. 그리는 쿼드는 간접 드로우라 생존 수만큼)
		uint32_t particlesSpawned = 0;   // 이번 프레임에 생성을 요청한 수 (spawnCount 합. 사망 목록이 모자라면 실제로는 덜 태어남)
		// ---- 디퍼드·클러스터드·SSAO (메인 뷰 기준) ----
		bool deferredPath = false;         // 이번 프레임 메인 뷰가 디퍼드로 그려졌는가
		uint32_t gbufferDraws = 0;         // G-buffer 패스 드로우 수
		uint32_t forwardOpaqueDraws = 0;   // 디퍼드 프레임에서 포워드로 간 불투명 드로우 수 (unlit·투과·코팅 노멀 맵)
		uint32_t lightsVisible = 0;        // 3a: 메인 카메라 절두체에 경계 구가 걸친 지역광 수 (방향광 제외)
		uint32_t lightsCulled = 0;         // 3a: 절두체 밖이라 그림자·클러스터에서 뺀 지역광 수
		uint32_t clusterLightRefs = 0;     // 클러스터 목록에 들어간 (클러스터, 조명) 쌍 수 = t31 원소 수
		uint32_t clusterMaxLights = 0;     // 한 클러스터의 최대 조명 수
		uint32_t clusterOverflow = 0;      // t31 용량이 모자라 버린 쌍 수 (0 이어야 함)
		// GPU 비닝이면 위 세 값과 아래 두 값은 몇 프레임 (≥ kFrameCount) 전 메인 뷰의 것 (Staging 리드백 — 기다리지 않음)
		uint32_t clusterTileRefs = 0;   // GPU 1단계: 타일 마스크에 켜진 (타일, 조명) 쌍 수
		uint32_t clusterTileMaxLights = 0;
		bool clusterGpu = false;             // 이번 프레임 비닝을 컴퓨트로 했는가
		uint32_t shadowViewsOffscreen = 0;   // 메인 카메라 절두체 밖 지역광에 간 그림자 뷰 수 (3a 를 끄면 늘어남 — 낭비된 아틀라스 타일)
		bool ssao = false;                   // 이번 프레임 메인 뷰에 SSAO 를 그렸는가
	};

	// 2048² 메인 아틀라스는 지역광(스팟·포인트) 전용: 512² 타일 16개를 4×4 로 배치 (D32 약 16MB). 스팟 =
	// 1타일, 포인트 = 연속 6타일. 방향광(캐스케이드 1~4 모두)은 이 아틀라스가 아니라 아래의 CSM 전용
	// 아틀라스에 그림.
	static constexpr uint32_t kShadowAtlasSize = 2048;
	static constexpr uint32_t kShadowTileSize = 512;   // 지역광 타일
	static constexpr uint32_t kShadowLocalTileCount = 16;
	static constexpr uint32_t kShadowPcfBorder = 3;   // PCF 5×5 반경 2 + 쌍선형 1 (텍셀)

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
	// 해상도를 바꾸려면 kCsmCascadeSize 와 CsmRect(RendererShadow.cpp)의 배치를 같이 고칠 것.
	static constexpr uint32_t kCsmAtlasWidth = 2048;
	static constexpr uint32_t kCsmAtlasHeight = 2048;
	static constexpr uint32_t kCsmCascadeSize[4] = { 1024, 1024, 1024, 1024 };

	// 그림자 뷰 하나 = 아틀라스의 사각형 하나 + 그 사각형에 그릴 광원 행렬. 조명마다 0개 이상 (방향광 1, 스팟
	// 1, 포인트 6, 캐스케이드 N).
	struct ShadowView
	{
		DirectX::XMFLOAT4X4 viewProj;        // 행우선 (올릴 때 전치)
		uint32_t x = 0, y = 0, size = 0;     // 아틀라스 안의 픽셀 사각형 (정사각형)
		uint32_t atlas = 0;                  // 0 = 메인 아틀라스(m_shadowMap), 1 = CSM 아틀라스(m_csmMap)
		DirectX::XMFLOAT4 params = {};       // b6 의 params 와 같은 뜻
		DirectX::XMFLOAT4 cullSphere = {};   // 단계 6. xyz = 광원 위치, w = 범위. w = 0 이면 컬링 안 함
		uint32_t lightIndex = 0;             // 씬 조명 번호 (Scene::GetLights 의 인덱스. BuildShadowViews 끝에서 t15 번호를 바꿔 둠)
	};
	uint32_t GetShadowViewCount() const { return m_shadowViewCount; }
	const ShadowView& GetShadowView(uint32_t index) const { return m_shadowViews[index]; }
	uint32_t GetSkippedShadowLights() const { return m_skippedShadowLights; }
	uint32_t GetUsedLocalTiles() const { return m_usedLocalTiles; }

	// ---- 디버그: 컬링 카메라 고정 (docs/plan/freeze-culling-camera-plan.html) ----
	// 켜면 그 순간 camera 의 위치·방향·렌즈를 사본으로 잡고, 켜 둔 항목을 그 사본 기준으로 계산함. 에디터 카메라를
	// 자유롭게 옮겨서 "고정된 시점에서 무엇이 버려졌는지"를 바깥에서 볼 수 있음. 그리는 일(b0·클러스터 상자·반투명 정렬·
	// TAA·파티클)은 늘 지금 카메라. 끄면 다시 매 프레임 현재 카메라를 따라감. 런타임 디버그 상태일 뿐이라 씬 파일에는
	// 저장하지 않음. 카메라는 호출자가 직접 넘김 (Render 는 한 프레임에 여러 번 불려 미리보기 카메라를 잡을 수 있음).
	struct CullFreezeParts
	{
		bool cascades = true;   // BuildShadowViews 의 캐스케이드 분할·광원 뷰 (프레임 공용 — 미리보기 그림자에도 걸림)
		bool objects = true;    // DrawItems 의 오브젝트 절두체 컬링 (메인 뷰만)
		bool lights = true;     // CullLights 의 절두체·그림자 점수 + 컬링된 지역광을 클러스터에서 뺌 (프레임 공용, 클러스터드일 때만 빛이 꺼짐)
	};
	void SetCullFreeze(bool freeze, const Camera& camera, CullFreezeParts parts = {});
	void SetCullFreezeParts(CullFreezeParts parts) { m_cullFreezeParts = parts; }   // 고정 중 항목만 바꿈 (카메라 사본은 그대로)
	bool IsCullFrozen() const { return m_cullFrozen; }
	CullFreezeParts GetCullFreezeParts() const { return m_cullFreezeParts; }
	const Camera& GetCullFreezeCamera() const { return m_cullFreezeCamera; }   // 오버레이용 (고정 중일 때만 의미 있음)
	bool IsCascadeFrozen() const { return m_cullFrozen && m_cullFreezeParts.cascades; }
	// 고정 시점에서 절두체 밖이라 컬링된 지역광인가 (씬 조명 번호). 고정 중이고 lights 항목이 켜져 있을 때만 true 가 나옴.
	bool IsLightCulledByFreeze(uint32_t sceneLightIndex) const;
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
	TextureHandle GetShadowMap() const { return m_shadowMap; }                // 디버그 표시용
	TextureHandle GetCsmMap() const { return m_csmMap; }                      // 디버그 표시용 (CSM 전용 아틀라스)

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
	// sourceId: 이 카메라를 만든 오브젝트 (GameObject::GetId). 에디터는 늘 같은 Camera 사본을 넘기므로 포인터로는 대상이 바뀐 것을
	// 알 수 없음 — id 가 바뀌면 미리보기 뷰의 지난 프레임(속도·TAA 히스토리)을 버림.
	// 이번 프레임의 dt (초) — 장면을 움직인 것과 같은 값 (Engine::Render 가 Time::GetDeltaTime 을 넘김, 시간 배율 전).
	// 모션 블러가 길이를 기준 fps 에 맞출 때 씀 (PostProcessSettings::motionBlurTargetFps).
	void SetFrameDeltaTime(float seconds) { m_frameDeltaTime = seconds; }
	void SetPreviewCamera(const Camera* camera, uint64_t sourceId = 0)
	{
		m_previewCamera = camera;
		m_previewSourceId = sourceId;
	}
	TextureHandle GetPreviewTexture() const { return m_previewView.color; }
	// 이번 프레임에 미리보기를 그릴 수 있는 상태면 그 크기를 돌려줌 (Render 의 previewActive 와 같은 조건). UI 시스템이 미리보기용
	// 레이아웃을 만들 때 씀.
	bool GetPreviewSize(uint32_t& width, uint32_t& height) const;
	// 메인 뷰 출력 크기 (에디터 = 씬 창, 게임 = 백버퍼)
	uint32_t GetMainViewWidth() const;
	uint32_t GetMainViewHeight() const;
	const Camera* GetPreviewCamera() const { return m_previewCamera; }
	// UI 텍스처의 픽셀 크기 (9-slice·preserveAspect·레이아웃). 처음 물으면 읽어 둠 (UI 가 그릴 때와 같은 선형 색공간). 실패면 false
	bool GetUITextureSize(const std::string& name, const Scene& scene, uint32_t& width, uint32_t& height);

	// 게임 UI (docs/plan/ui-canvas-plan.html). UISystem::Build 가 만든 그리기 목록을 Render 전에 넘김 — Render 안에서 올리고
	// 톤 패스 뒤에 그림. 목록은 Render 가 끝날 때까지 살아 있어야 함. nullptr 이거나 비어 있으면 UI 패스 없음.
	void SetUIDrawList(const UIDrawList* list) { m_uiDrawList = list; }

	// 후처리 단계별 썸네일 (에디터의 "Post Stages" 창). 창이 열려 있는 동안 매 프레임 RequestPostCapture 를 부르면
	// 다음 Render 가 단계마다 HDR 결과를 톤매핑한 작은 텍스처에 그림. 요청이 없으면 비용이 없음. 메인 카메라,
	// 에디터(오프스크린) 전용. 단계: 0 Scene (후처리 전), 1 TAA, 2 Motion, 3 Blur, 4 Bloom (꺼진 효과는
	// active = false), 5 Final (후처리 + 색 효과). 단계 0~4 는 색 효과를 뺀 그림임.
	static constexpr uint32_t kPostStageCount = 6;   // Scene, TAA, Motion, Blur, Bloom, Final
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
	// 속도 버퍼·TAA 의 "지난 프레임"을 모두 버림: 뷰마다 지난 카메라 행렬·TAA 히스토리, 오브젝트마다 지난 World·팔레트.
	// 씬 불러오기·새 씬·재생 시작/정지처럼 화면이 끊기는 곳에서 부름 — 안 부르면 한 프레임 동안 큰 속도·이전 화면이 남음.
	void ResetTemporalHistory();

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
		TextureHandle boundSpecular;   // KHR_materials_specular
		TextureHandle boundSpecularColor;
		TextureHandle boundClearcoat;   // KHR_materials_clearcoat
		TextureHandle boundClearcoatRoughness;
		TextureHandle boundClearcoatNormal;
		TextureHandle boundTransmission;   // KHR_materials_transmission
		SamplerHandle boundSampler;
		uint64_t uploadedFrame = UINT64_MAX;   // 이번 프레임에 상수를 올렸는지

		// 셋을 만들 때 본 입력의 사본. 프레임마다 이것과 재질을 비교해, 같으면 텍스처 이름 → 핸들 조회를
		// 통째로 건너뜀 (바뀌는 것은 재질 편집·샘플러 덮어쓰기·텍스처 캐시 비우기뿐).
		std::string albedoName, normalName, metallicRoughnessName, occlusionName, emissiveName, specularName, specularColorName, clearcoatName, clearcoatRoughnessName, clearcoatNormalName, transmissionName;
		bool albedoSrgb = true;
		SamplerHandle sampler;
		uint64_t textureGeneration = UINT64_MAX;   // 셋을 만들 때의 TextureCache::GetGeneration()
	};

	// 이번 프레임에 그릴 오브젝트 하나의 계산 결과. 월드 행렬(부모 계층 곱)·역행렬·경계 구를 프레임에 한 번만
	// 구해 두고, 그림자 뷰 여러 개 + 메인 + 미리보기 패스의 모든 드로우가 이것을 다시 씀.
	struct ObjectData
	{
		DirectX::XMFLOAT4X4 world;                                       // 전치해서 저장 (b1 에 그대로 올림)
		DirectX::XMFLOAT4X4 worldInvTranspose;                           // 올리는 값은 W⁻¹ (ShaderConstants.h)
		DirectX::XMFLOAT4 sphere;                                        // 월드 경계 구 (xyz = 중심, w = 반지름). 컬링용
		const std::vector<DirectX::XMFLOAT4X4>* skinPalette = nullptr;   // 스키닝 오브젝트의 조인트 팔레트 (씬이 소유, 이번 프레임 동안 유효). 비면 단위행렬
		bool skinned = false;                                            // 스키닝 메시면 드로우마다 b7 에 팔레트를 올림
		// 속도 버퍼: 지난 프레임 World (전치, b1 prevWorld). 지난 프레임에 없던 오브젝트면 world 와 같음.
		DirectX::XMFLOAT4X4 prevWorld;
		// 지난 프레임 팔레트의 위치 (m_motion[지난].palettes 안). 없으면 prevPaletteCount = 0 → 이번 팔레트를 b13 에도 올림.
		uint32_t prevPaletteOffset = 0;
		uint32_t prevPaletteCount = 0;
	};
	// 오브젝트 하나의 "지난 프레임" (GameObject::GetId 로 찾음). 팔레트는 MotionHistory::palettes 안의 위치.
	struct PrevObject
	{
		DirectX::XMFLOAT4X4 world;   // 전치 (ObjectData::world 와 같은 관례)
		uint32_t paletteOffset = 0;
		uint32_t paletteCount = 0;
	};
	// 프레임 하나의 오브젝트 상태 모음. 두 벌을 두고 BuildDrawList 가 프레임마다 맞바꿈 — 이번 것을 쓰면서 지난 것을 읽음.
	// 맞바꾸기만 하므로 지워진 오브젝트의 항목은 한 프레임 뒤 저절로 사라짐.
	struct MotionHistory
	{
		std::unordered_map<uint64_t, PrevObject> objects;
		std::vector<DirectX::XMFLOAT4X4> palettes;   // 씬의 팔레트 그대로 (전치 전)
	};
	// 뷰 하나의 "지난 프레임" (ViewTarget::temporal). 메인과 미리보기는 서로 다른 카메라라 따로 가짐.
	struct TemporalState
	{
		DirectX::XMFLOAT4X4 prevUnjitteredViewProj;   // 전치 (b0 그대로). hasPrev 가 거짓이면 의미 없음
		const Camera* lastCamera = nullptr;           // 지난 프레임에 이 뷰를 그린 카메라. 바뀌면 (미리보기 대상·재생 전환) 리셋
		uint32_t frameIndex = 0;                      // 이 뷰를 그린 프레임 수 (TAA 지터 위상)
		bool hasPrev = false;
		bool historyValid = false;   // TAA 히스토리에 쓸 만한 지난 결과가 있음
		uint32_t current = 0;        // TAA: 이번 프레임이 쓸 히스토리 (0/1)
	};

	struct DrawItem
	{
		PipelineHandle pipeline;          // 메인 패스 PSO (정렬 키)
		PipelineHandle shadowPipeline;    // 그림자 패스 PSO. unlit 재질은 빈 핸들 (그림자를 만들지 않음)
		const Material* material;         // 정렬 키
		const Mesh* mesh;                 // 정렬 키
		const GpuMaterial* gpuMaterial;   // BuildDrawList 가 만들어 둠 (드로우 루프에서는 생성하지 않음)
		const GpuMesh* gpuMesh;
		PipelineHandle gbufferPipeline;   // G-buffer 패스 PSO. deferred 가 거짓이면 빈 핸들
		bool deferred = false;            // G-buffer 에 담을 수 있는 재질 (CanUseDeferred). 디퍼드 프레임에서 포워드 대신 G-buffer 패스로 감
		bool blended = false;             // 알파 블렌드·투과 재질. 불투명·하늘 뒤에 먼 것부터 따로 그림 (정렬 키의 맨 앞)
		uint32_t object = 0;              // m_objects 인덱스
		uint32_t indexStart = 0;          // 서브메시 범위
		uint32_t indexCount = 0;
	};

	// 뷰 하나(메인 씬 뷰, 카메라 미리보기)가 그리는 데 필요한 텍스처 묶음. 메인과 미리보기가 같은 함수로
	// 만들어지고 그려짐. G-buffer·SSAO 텍스처를 추가할 때 여기에 넣으면 두 뷰가 함께 받음.
	struct PostScratch;
	struct ViewTarget;

	// ---- 셰이더·레이아웃·패스 표 (Renderer.cpp 의 kShaders, kPasses) ----
	// 셰이더를 추가할 때: ShaderId 에 한 줄, kShaders 에 한 줄. 로드·핫리로드 감시·검증·해제는 표를 돌며 함.
	enum ShaderId : uint32_t
	{
		Shader_BasicVS,
		Shader_BasicPS,
		Shader_ShadowVS,
		Shader_SkyVS,
		Shader_SkyPS,
		Shader_ScreenVS,
		Shader_TonemapPS,
		Shader_PostPS,
		Shader_BasicSkinVS,
		Shader_ShadowSkinVS,
		Shader_DebugLineVS,
		Shader_DebugLinePS,
		Shader_BlurCS,
		Shader_BlurWeightsCS,
		Shader_ParticleResetCS,
		Shader_ParticleSimCS,
		Shader_ParticleArgsCS,
		Shader_ParticleEmitCS,
		Shader_ParticleSortInitCS,
		Shader_ParticleSortStepCS,
		Shader_ParticleVS,
		Shader_ParticlePS,
		Shader_GBufferPS,
		Shader_DeferredLightPS,
		Shader_SsaoPS,
		Shader_SsaoBlurPS,
		Shader_VelocityDebugPS,
		Shader_MotionBlurPS,
		Shader_TaaPS,
		Shader_ClusterViewCS,
		Shader_ClusterTileCS,
		Shader_ClusterCountCS,
		Shader_ClusterScanCS,
		Shader_ClusterFillCS,
		Shader_OceanVS,
		Shader_OceanPS,
		Shader_OceanGBufferPS,
		Shader_UIVS,
		Shader_UIPS,
		Shader_Count
	};
	enum LayoutId : uint32_t
	{
		Layout_Frame,
		Layout_Object,
		Layout_Material,
		Layout_Post,
		Layout_BlurCompute,
		Layout_BlurWeights,
		Layout_ParticleSim,
		Layout_ParticleStep,
		Layout_ParticleDraw,
		Layout_ParticleDepth,
		Layout_ParticleCollide,
		Layout_ParticleSort,
		Layout_ParticleSortStep,
		Layout_GBuffer,
		Layout_Ssao,
		Layout_Temporal,
		Layout_ClusterBin,
		Layout_UICommon,    // b4 UI 상수 + s0 (UI 패스에 하나)
		Layout_UITexture,   // t0 (텍스처마다 하나)
		Layout_Count
	};
	// 패스 = (VS, PS, 레이아웃 목록). PSO Desc 의 셰이더·bindingLayouts 가 이 표에서 나오고, 바인딩 대조도 패스
	// 단위로 함 (그 패스의 레이아웃에 선언된 슬롯만 통과).
	// Pass_MainSkin / Pass_ShadowSkin 은 스키닝 메시용 (정점 포맷 PositionColorNormalTexcoordTangentSkin). 스킨 VS 만 다르고 PS·레이아웃은 같음.
	// Pass_GBuffer / Pass_GBufferSkin: 디퍼드 G-buffer (MRT 6장 = 발광 hdr + GB0~GB3 + 속도, 재질 셋까지 메인과 같은 레이아웃). Pass_DeferredLight: 풀스크린
	// 라이팅 { 프레임, G-buffer }. Pass_Ssao / Pass_SsaoBlur: 풀스크린 { SSAO }.
	enum PassId : uint32_t
	{
		Pass_Main,
		Pass_Shadow,
		Pass_Sky,
		Pass_Tonemap,
		Pass_Post,
		Pass_MainSkin,
		Pass_ShadowSkin,
		Pass_DebugLine,
		Pass_Particle,
		Pass_GBuffer,
		Pass_GBufferSkin,
		Pass_DeferredLight,
		Pass_Ssao,
		Pass_SsaoBlur,
		Pass_VelocityDebug,
		Pass_MotionBlur,
		Pass_Taa,
		Pass_Ocean,          // OceanVS + OceanPS (메인과 같은 셋)
		Pass_GBufferOcean,   // OceanVS + OceanGBufferPS
		Pass_UI,             // 게임 UI 캔버스 { UI 공통, UI 텍스처 }
		Pass_UIWorld,        // World 캔버스: Pass_UI + 읽기 전용 깊이 검사 (LessEqual, 쓰기 없음)
		Pass_Count
	};
	// 셋 인덱스 = 그 패스의 PipelineStateDesc::bindingLayouts 인덱스 (CommandList::SetResourceSet 의 첫 인자).
	// 메인 { 프레임, 오브젝트, 재질 }, 그림자 { 프레임, 오브젝트 }, 스카이 { 프레임 }, 후처리 { 후처리 }.
	static constexpr uint32_t kSetFrame = 0;
	static constexpr uint32_t kSetObject = 1;
	static constexpr uint32_t kSetMaterial = 2;
	static constexpr uint32_t kSetPost = 0;
	static constexpr uint32_t kSetParticle = 1;   // 파티클 그리기 { 프레임, 파티클, 깊이 }
	static constexpr uint32_t kSetParticleDepth = 2;
	static constexpr uint32_t kSetGBuffer = 1;                        // 디퍼드 라이팅 { 프레임, G-buffer }
	static constexpr uint32_t kSetSsao = 0;                           // SSAO·블러 { SSAO }
	static constexpr uint32_t kSetTemporal = 0;                       // 속도 디버그·모션 블러·TAA { 시간 }
	static constexpr uint32_t kSetUICommon = 0;                       // 게임 UI { UI 공통, UI 텍스처 }
	static constexpr uint32_t kSetUITexture = 1;
	static constexpr Format kVelocityFormat = Format::R16G16_FLOAT;   // 속도 RT. uv(이번) − uv(지난), Shaders/Velocity.hlsli
	// 컴퓨트 패스 = (CS, 레이아웃 목록). 그래픽스 패스 표와 따로 둠 — PSO Desc 가 다르고 (ComputePipelineDesc),
	// 바인딩 대조의 스테이지가 Compute 임.
	enum ComputePassId : uint32_t
	{
		ComputePass_Blur,
		ComputePass_BlurWeights,
		ComputePass_ParticleReset,
		ComputePass_ParticleSim,
		ComputePass_ParticleArgs,
		ComputePass_ParticleEmit,
		ComputePass_ParticleSortInit,
		ComputePass_ParticleSortStep,
		ComputePass_ClusterView,
		ComputePass_ClusterTile,
		ComputePass_ClusterCount,
		ComputePass_ClusterScan,
		ComputePass_ClusterFill,
		ComputePass_Count
	};
	static constexpr uint32_t kSetBlurCompute = 0;
	static constexpr uint32_t kSetBlurWeights = 0;
	static constexpr uint32_t kSetParticleSim = 0;
	static constexpr uint32_t kSetParticleCollide = 1;    // 적분 패스만 { 적분, 충돌 }
	static constexpr uint32_t kSetParticleSortStep = 1;   // 정렬 패스 { 정렬, 단계 }
	static constexpr uint32_t kSetClusterBin = 0;         // GPU 클러스터 비닝 { 비닝 } (뷰마다 한 벌)
	static constexpr uint32_t kMaxParticleViews = 2;      // 알파 이미터의 정렬 결과를 따로 두는 뷰: 0 메인, 1 카메라 미리보기
	struct ShaderInfo;                                    // 파일·진입점·스테이지·이름
	struct PassInfo;                                      // 이름·VS·PS·레이아웃 목록
	struct ComputePassInfo;                               // 이름·CS·레이아웃 목록
	static const ShaderInfo kShaders[Shader_Count];
	static const PassInfo kPasses[Pass_Count];
	static const ComputePassInfo kComputePasses[ComputePass_Count];

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
	// b0: view/proj·화면 크기·클러스터 깊이 분할은 이 카메라·타깃 기준, 나머지는 프레임 공통 값. ssaoReady = 이 뷰에 SSAO 결과가 있음.
	// view: 지난 프레임 카메라 행렬을 읽고 이번 것을 남김 (속도 버퍼). 뷰마다 프레임에 한 번만 부를 것.
	void UploadPerFrameConstants(const Camera& camera, ViewTarget& target, float totalTime, bool ssaoReady);
	void RenderShadowPass(RHI::CommandList& cmd);
	void RenderShadowAtlasPass(RHI::CommandList& cmd, uint32_t atlas);   // atlas 0 = 메인(m_shadowMap), 1 = CSM(m_csmMap). 그 아틀라스의 뷰만 그림
	// 뷰 하나의 씬 패스 (메인·미리보기 공통): view.hdr + view.depth 에 불투명 드로우와 스카이박스를 그림.
	// b0 는 호출 전에 그 카메라로 올려 둘 것. 끝나면 view.hdr 는 ShaderResource 상태.
	// 패스 두 개: (1) 불투명·하늘·블렌딩 항목 (깊이 씀) (2) 파티클·디버그 선 (깊이 읽기 전용 — 셰이더가 깊이를 SRV 로도
	// 읽음). viewIndex 0 = 메인 뷰: (1) 과 (2) 사이에서 파티클 시뮬레이션 (프레임에 한 번). 알파 이미터 정렬은 뷰마다.
	// 끝나면 view.depth 는 (2) 를 열었으면 DepthRead, 아니면 DepthWrite.
	void RenderScenePass(RHI::CommandList& cmd, const Scene& scene, ViewTarget& view, const Camera& camera, const char* debugName, uint32_t viewIndex);
	// 씬 패스의 드로우 루프 (절두체 컬링). mode 가 어느 항목을 그릴지 정함:
	//   All         포워드 프레임: 불투명 전부 → 하늘 → 블렌딩 (예전 경로)
	//   GBuffer     디퍼드 G-buffer 패스: deferred 항목만 (G-buffer PSO)
	//   ForwardRest 디퍼드 프레임의 포워드 패스: deferred 가 아닌 불투명 → 하늘 → 블렌딩
	enum class DrawMode : uint8_t
	{
		All,
		GBuffer,
		ForwardRest
	};
	// camera = 그리는 카메라 (반투명 정렬의 눈 위치), cullCamera = 오브젝트 절두체 컬링 카메라 (보통 같음, 고정 중 메인 뷰는 사본).
	void DrawItems(RHI::CommandList& cmd, const Scene& scene, const Camera& camera, const Camera& cullCamera, DrawMode mode);
	// 이 뷰가 오브젝트 컬링에 쓸 카메라: 고정 중이고 objects 항목이 켜져 있으면 메인 뷰(viewIndex 0)만 사본.
	const Camera& ObjectCullCamera(const Camera& drawCamera, uint32_t viewIndex) const
	{
		return m_cullFrozen && m_cullFreezeParts.objects && viewIndex == 0 ? m_cullFreezeCamera : drawCamera;
	}

	// ---- 디퍼드 (2단계, RendererDeferred.cpp) ----
	// G-buffer 에 담을 수 있는 재질인가: 불투명(블렌딩 아님)·조명 받음(unlit 아님)·투과 0·코팅 노멀 맵 없음.
	bool CanUseDeferred(const Material& material) const;
	// 이번 프레임 이 뷰를 디퍼드로 그리는가: 설정이 켜져 있고, G-buffer 가 있고, 디버그 뷰가 5(UV)가 아님.
	bool UseDeferred(const ViewTarget& view) const;
	bool CreateGBuffer(ViewTarget& view, const std::string& base);   // CreateViewTarget 이 부름. 실패해도 뷰는 유효 (포워드로 감)
	void DestroyGBuffer(ViewTarget& view);
	// 디퍼드 씬 패스: G-buffer 패스 → (SSAO) → 라이팅 패스 → 포워드 패스. RenderScenePass 가 디퍼드일 때 부름.
	void RenderDeferredScene(RHI::CommandList& cmd, const Scene& scene, ViewTarget& view, const Camera& camera, const char* debugName, uint32_t viewIndex);
	PipelineHandle GetGBufferPipelineFor(const Material& material, VertexFormat vertexFormat, bool mirrored, bool ocean = false);
	PipelineHandle GetDeferredLightPipeline();

	// ---- 3a·3b: 조명 컬링·그림자 우선순위·클러스터 (RendererClusters.cpp) ----
	// 조명 하나의 경계 구 (월드). 방향광은 w < 0 (무한). 스팟은 원뿔을 담는 가장 작은 구, 포인트는 (위치, 범위).
	static DirectX::XMFLOAT4 ComputeLightBounds(const GpuLight& light);
	// 뷰 z → 클러스터 슬라이스 식의 계수 (b0 clusterDepth). 원근: (Z / log2(f/n), −Z · log2(n) / log2(f/n), 1, 0),
	// 직교: (Z / (f − n), −n · Z / (f − n), 0, 0). 셰이더 ClusterSlice 와 C++ 비닝이 같은 식을 씀.
	static DirectX::XMFLOAT4 ClusterDepthParams(const Camera& camera);
	// 3a: 메인 카메라 절두체로 지역광을 거르고 (m_lightVisible), 그림자 후보의 점수를 매겨 높은 순으로 정렬한 조명 번호 목록.
	void CullLights(const Camera& camera);
	// 클러스터 비닝: 이 카메라·타깃 크기로 상자 3456 개의 조명 목록을 만듦 (m_clusterGridScratch / m_clusterIndexScratch[viewIndex]).
	// 메인·미리보기 두 뷰를 Prepare 에서 미리 만들어 두고 (버퍼 크기를 한 번에 맞춤), 각 뷰 패스 직전에 UploadClusters 로 올림.
	void BuildClusters(const Camera& camera, uint32_t width, uint32_t height, uint32_t viewIndex);
	// t30·t31 용량을 두 뷰 중 큰 쪽에 맞춤 (모자라면 두 배씩, 상한 kMaxClusterLightRefs). 다시 만들면 프레임 셋도 다시 만들게 함.
	void EnsureClusterCapacity();
	void UploadClusters(uint32_t viewIndex);   // t30·t31 에 그 뷰의 목록을 올림 (Dynamic — 클러스터드가 꺼져도 프레임마다 올림)

	// ---- GPU 클러스터 비닝 (RendererClusterBin.cpp, docs/plan/gpu-cluster-binning-record.html) ----
	// 이번 프레임에 GPU 경로를 쓸 수 있는가: 설정이 켜져 있고, 컴퓨트 파이프라인 다섯 개와 버퍼가 준비됨. 버퍼는 조명 버퍼 용량에
	// 맞춰 (다시) 만듦 — UploadLights 뒤에 부름.
	bool PrepareGpuClusters();
	// Prepare 구간: t7 (월드 경계 구) 를 올리고, 뷰의 b13 (뷰 행렬·투영 항·슬라이스 경계) 을 올림. 메인 뷰는 지난 통계도 읽음.
	void UploadLightBounds();
	void UploadClusterBinConstants(const Camera& camera, uint32_t viewIndex);
	void ReadGpuClusterStats();
	// 렌더 패스 밖에서: 디스패치 다섯 번 → t30·t31 (GPU 버퍼) 을 ShaderResource 로. 메인 뷰는 통계를 Staging 으로 복사.
	void DispatchClusterBinning(RHI::CommandList& cmd, uint32_t viewIndex);
	void DestroyGpuClusterResources();

	// ---- SSAO (5단계, RendererSsao.cpp) ----
	bool CreateSsaoTargets(ViewTarget& view, const std::string& base);
	void DestroySsaoTargets(ViewTarget& view);
	void UploadSsaoConstants(const SsaoSettings& settings);   // b12. 커널은 설정의 sampleCount 가 바뀔 때만 다시 만듦
	// depth (DepthRead) 와 GB1 (ShaderResource) 을 읽어 ssaoRaw → 블러 → ssaoBlur. 끝나면 ssaoBlur 가 ShaderResource.
	void RenderSsao(RHI::CommandList& cmd, ViewTarget& view);
	PipelineHandle GetSsaoPipeline(bool blur);
	// 디버그 선 (본·Raycast 등). 선은 Engine 이 소유한 DebugDraw 가 모으고, Renderer 는 읽기만 함: UploadDebugLines 가 그 월드 공간
	// 선 목록을 프레임마다 동적 정점 버퍼에 올리고, DrawDebugLines 가 씬 패스 끝(하늘 뒤)에 깊이 테스트 없이 그림.
	void UploadDebugLines(const DebugDraw& debugDraw);
	void DrawDebugLines(RHI::CommandList& cmd);
	PipelineHandle GetDebugLinePipeline();

	// ---- 게임 UI 캔버스 (RendererUI.cpp) ----
	// UploadUI: 업로드 구간 안에서 텍스처를 읽고(셋 캐시) 정점·인덱스·캔버스 행렬을 프레임에 한 번 올림. 목록이 비면 아무것도
	// 올리지 않고 m_uiReady = false (D3D12: 그 프레임에 쓰지 않은 Dynamic 버퍼는 묶지 않음).
	// RenderCanvasPass: 뷰 하나(0 메인, 1 미리보기)의 명령을 target 에 UNORM 뷰로 그리고 endState 로 돌려놓음. 명령이 없으면 아무것도 안 함.
	void UploadUI(const Scene& scene);
	// depth: 그 뷰의 깊이 (World 캔버스가 장면에 가려짐). 비어 있으면 World 캔버스 명령을 건너뜀.
	void RenderCanvasPass(RHI::CommandList& cmd, uint32_t viewIndex, TextureHandle target, TextureHandle depth, ResourceState endState, uint32_t width, uint32_t height);
	PipelineHandle GetUIPipeline();
	ResourceSetHandle GetUITextureSet(TextureHandle texture);
	void UploadUIAtlas(UIAtlasImage& atlas);
	void DestroyUIResources();

	// ---- GPU 파티클 (VFX, RendererParticles.cpp) ----
	// BuildDrawList 가 이미터를 모으고 (CollectEmitter), 업로드 구간 안에서 버퍼·셋을 맞추고 상수를 올림
	// (UpdateParticleEmitters). RenderScenePass 가 메인 뷰 패스 1 (불투명 깊이) 뒤에 컴퓨트로 한 번 시뮬레이션하고
	// (SimulateParticles), 뷰마다 알파 이미터를 정렬한 뒤 (SortParticles) 읽기 전용 깊이 패스 2 에서 그림
	// (DrawParticles — 메인·미리보기 뷰가 같은 버퍼를 읽음). 구조·원리: docs/plan/vfx-gpu-particles-record.html 12장.
	void CollectEmitter(const GameObject& object, const DirectX::XMMATRIX& world);
	void UpdateParticleEmitters(float totalTime, const Camera& camera);
	void SimulateParticles(RHI::CommandList& cmd, const ViewTarget& view);
	void SortParticles(RHI::CommandList& cmd, uint32_t viewIndex);   // 알파 이미터만. 렌더 패스 밖, 그 뷰의 파티클 패스 전
	void DrawParticles(RHI::CommandList& cmd, const ViewTarget& view, uint32_t viewIndex);
	bool EnsureParticleSortSteps(uint32_t count);   // count (2 의 거듭제곱) 를 정렬할 단계 상수 버퍼·셋까지 만듦
	PipelineHandle GetParticlePipeline();
	void DestroyGpuEmitters();

	// ---- Gerstner 바다 (RendererOcean.cpp, docs/plan/ocean-gerstner-plan.html) ----
	// BuildDrawList 가 오브젝트마다 CollectOcean 을 불러 첫 번째 바다의 파도를 이번 프레임 것으로 모음 (참 = 이 오브젝트가 바다).
	// 그 뒤 UploadOceanConstants 가 b9 에 위상을 올림 — 바다가 없어도 프레임마다 한 번 (파도 0 개).
	bool CollectOcean(const GameObject& object, const DirectX::XMMATRIX& world);
	void UploadOceanConstants(float totalTime);
	struct GpuEmitter;
	void DestroyGpuEmitter(GpuEmitter& gpu, bool includeConstants);   // includeConstants = false: 크기 변경 (b8 버퍼는 다시 씀)
	void DrawSkybox(RHI::CommandList& cmd);                           // 전체 화면 삼각형 하나 (Draw(3), 정점 버퍼 없음)
	// HDR: 씬 패스가 그린 HDR 텍스처(hdrSet 의 t13)를 읽어 노출·ACES 를 적용하고 output 에 sRGB 로 씀.
	// output 은 RenderTarget 으로 전이했다가 끝나면 outputAfter 로 둠. HDR 텍스처는 이미 ShaderResource
	// 상태여야 함. srcW/srcH = HDR 텍스처 크기, dstW/dstH = output 크기 (썸네일은 더 작음).
	// skipGrade 가 참이면 색 효과(GreyScale 등)를 건너뜀.
	void RenderTonemapPass(RHI::CommandList& cmd, ResourceSetHandle hdrSet, TextureHandle output, ResourceState outputAfter, const char* debugName, uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH, bool skipGrade = false);
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
			TextureHandle texture;   // 레벨 i 는 hdr 의 1/2^(i+1) 크기
			ResourceSetHandle set;   // Layout_Post: t13 = texture
			uint32_t width = 0;
			uint32_t height = 0;
		};
		uint32_t width = 0;   // 이 스크래치가 맞춘 hdr 크기. 바뀌면 전부 다시 만듦
		uint32_t height = 0;
		TextureHandle temp;          // 블러 가로 패스의 결과 (hdr 와 같은 크기·포맷). 블러가 처음 켜질 때 만듦
		ResourceSetHandle tempSet;   // Layout_Post: t13 = temp
		// 컴퓨트 블러의 셋 (Layout_BlurCompute: b4, t13 입력, u0 출력). [0] 가로 = hdr → temp, [1] 세로 = temp → hdr.
		// hdr 를 가리키므로 hdr 가 다시 만들어지면 같이 버려야 함 — DestroyViewTarget 이 DestroyPostScratch 를 부름.
		ResourceSetHandle blurComputeSets[2];
		int8_t blurPath = -1;             // 지난 프레임의 블러 경로 (0 PS, 1 컴퓨트). 바뀔 때만 로그를 남김 (검증용)
		BloomLevel bloom[kBloomLevels];   // 블룸 축소 체인. 블룸이 처음 켜질 때 만듦
		uint32_t bloomCount = 0;
	};
	struct ViewTarget
	{
		TextureHandle color;                    // 최종 출력 RGBA8 (TYPELESS: sRGB RTV + UNORM SRV, ImGui 가 읽음). 게임 모드의 메인 뷰는 비어 있음 — 백버퍼에 씀
		TextureHandle depth;                    // D24S8. DepthStencil | ShaderResource 라 후처리가 SRV 로 읽을 수 있음
		TextureHandle hdr;                      // R16G16B16A16_FLOAT. 씬 패스의 렌더 타깃
		ResourceSetHandle hdrSet;               // Layout_Post: t13 = hdr (톤 패스·후처리의 입력)
		ResourceSetHandle particleDepthSet;     // Layout_ParticleDepth: t23 = depth (파티클 PS, 소프트 파티클)
		ResourceSetHandle particleCollideSet;   // Layout_ParticleCollide: b9, t23 = depth (파티클 적분 CS, 충돌. 메인 뷰만 씀)
		PostScratch post;                       // 블러·블룸 스크래치 (hdr 크기를 따름)
		// 디퍼드 (2단계): G-buffer 4장 (배치는 Shaders/GBuffer.hlsli) + 라이팅 PS 가 읽는 셋 (t23 깊이, t25~t28, t29 SSAO)
		TextureHandle gbuffer[4];
		ResourceSetHandle gbufferSet;
		// SSAO (5단계): 원본(raw)과 블러 결과. R8_UNORM, 뷰 크기. ssaoSet = { b0, b12, t23 깊이, t26 노멀, t29 흰색 },
		// ssaoBlurSet = 같은 것에 t29 = ssaoRaw. 라이팅은 G-buffer 셋의 t29 로 ssaoBlur 를 읽음.
		TextureHandle ssaoRaw;
		TextureHandle ssaoBlur;
		ResourceSetHandle ssaoSet;
		ResourceSetHandle ssaoBlurSet;
		// 속도 버퍼 (R16G16_FLOAT, 뷰 크기): G-buffer 패스·포워드 패스가 씀. temporalSet = Layout_Temporal { b0, b4, t13 hdr, t23 깊이,
		// t7 속도, t24 흰 텍스처, s6 } — 속도 디버그·모션 블러가 읽음. temporal = 지난 프레임 카메라 (속도의 "이전").
		TextureHandle velocity;
		ResourceSetHandle temporalSet;
		ResourceSetHandle velocityDebugSet;   // 같은 레이아웃, t13 = 흰 텍스처 — 속도 디버그는 hdr 에 쓰므로 hdr 를 입력으로 꽂지 않음 (SRV·RTV 겹침)
		// TAA 히스토리 (RGBA16F 두 장, 핑퐁). TAA 가 처음 켜질 때 만듦 (EnsureTaaTargets). taaSet[i] = 시간 셋 (t13 hdr, t24 history[1−i])
		// 이고 출력은 history[i]. historyPostSet[i] = 후처리 셋 (t13 history[i]) — 결과를 hdr 로 되돌려 복사할 때.
		TextureHandle history[2];
		ResourceSetHandle taaSet[2];
		ResourceSetHandle historyPostSet[2];
		TemporalState temporal;
		uint32_t width = 0;
		uint32_t height = 0;
		bool IsValid() const { return depth.IsValid() && hdr.IsValid() && hdrSet.IsValid(); }
		bool HasGBuffer() const { return gbufferSet.IsValid(); }   // 디퍼드로 그릴 수 있는가 (G-buffer 생성 실패면 포워드)
		bool HasSsao() const { return ssaoSet.IsValid() && ssaoBlurSet.IsValid(); }
	};
	// withColor: 최종 출력 텍스처도 만듦 (에디터의 씬 뷰·미리보기). 거짓이면 색은 백버퍼가 맡음 (게임 모드).
	// 실패하면 만든 것을 모두 지우고 false.
	bool CreateViewTarget(ViewTarget& view, uint32_t width, uint32_t height, bool withColor, const char* name);
	void DestroyViewTarget(ViewTarget& view);
	// Layout_Temporal 셋 (b0, b4, t13 = input, t23 = view.depth, t7 = view.velocity, t24 = history, s6). 속도 디버그·모션 블러·TAA.
	ResourceSetHandle CreateTemporalSet(const ViewTarget& view, TextureHandle input, TextureHandle history, const char* debugName);
	// 게임 모드(오프스크린이 아님): 출력 크기(SetOutputSize)에 맞는 메인 뷰 타깃을 보장함.
	void EnsureGameTarget();
	// 필요한 것만 만듦 (needBlur → temp + 컴퓨트 블러 셋, needBloom → 축소 체인). 크기가 바뀌었으면 전부 버리고
	// 다시 만듦. hdr 는 컴퓨트 블러 셋이 가리킴. 실패하면 false (컴퓨트 셋만 실패하면 true — PS 블러로 감).
	bool EnsurePostScratch(PostScratch& scratch, TextureHandle hdr, uint32_t width, uint32_t height, bool needTemp, bool needBloom);
	void DestroyPostScratch(PostScratch& scratch);
	// view.hdr 가 ShaderResource 상태일 때 부르고, 끝나도 ShaderResource 상태임. 효과가 없어도 깊이를 DepthRead 로 두고
	// TAA 히스토리 유효 여부를 갱신함.
	// capture 가 참이면 단계마다 썸네일을 그림 (CapturePostStage).
	void ApplyHdrEffects(RHI::CommandList& cmd, ViewTarget& view, bool capture);
	void EnsureStageThumbs(uint32_t hdrWidth, uint32_t hdrHeight);
	void DestroyStageThumbs();
	// 단계별 썸네일 번호 (kPostStageCount 칸). ApplyHdrEffects 의 패스 순서와 같음.
	enum PostStage : uint32_t
	{
		PostStage_Scene,
		PostStage_Taa,
		PostStage_Motion,
		PostStage_Blur,
		PostStage_Bloom,
		PostStage_Final
	};
	void CapturePostStage(RHI::CommandList& cmd, uint32_t stage, const char* name, ResourceSetHandle hdrSet, uint32_t hdrWidth, uint32_t hdrHeight, bool skipGrade);
	PipelineHandle GetPostPipeline(bool additive);   // FP16 출력, 깊이 없음. additive = One/One 블렌드 (블룸 합성)
	// 컴퓨트 패스의 PSO (kComputePasses 표). 처음 부를 때 만들고, 실패하면 빈 핸들을 기억해 매 프레임 다시 묻지 않음.
	// 블러: 빈 핸들이면 ApplyHdrEffects 가 PS 블러로 감. 가중치: 빈 핸들이면 블러를 건너뜀 (두 경로 모두 가중치 버퍼를 읽음).
	ComputePipelineHandle GetComputePipeline(ComputePassId pass);
	// 가우시안 가중치를 m_blurWeights 에 씀 (컴퓨트). PostParams(b4)의 시그마·반경을 쓰므로 호출 전에 올려 둘 것.
	// 끝나면 버퍼는 ShaderResource 상태 (PS·CS 블러가 t20 으로 읽음).
	void DispatchBlurWeights(RHI::CommandList& cmd, ComputePipelineHandle pipeline, uint32_t radius);
	// 블러 한 방향을 컴퓨트로: input(SRV) → output(UAV). 상태 전이(output → UnorderedAccess → ShaderResource)까지 함.
	// PostParams(b4)는 호출 전에 올려 둘 것.
	void DispatchBlurCompute(RHI::CommandList& cmd, ComputePipelineHandle pipeline, ResourceSetHandle set, TextureHandle output, uint32_t width, uint32_t height);
	void UploadPostParams(const PostParamsConstants& params);
	PipelineHandle GetTemporalPipeline(PassId pass);   // Layout_Temporal 풀스크린 패스의 PSO (FP16 출력 1장). m_temporalPipelines 캐시
	bool EnsureTaaTargets(ViewTarget& view);           // 히스토리 두 장과 셋. 실패하면 거짓 (그 프레임은 TAA 를 건너뜀)
	void DestroyTaaTargets(ViewTarget& view);
	bool TaaActive() const;                                                        // 이번 프레임에 TAA 를 돌리는가 (켜짐 + 디버그 뷰 아님). 지터도 이것을 따름
	ResourceSetHandle CreatePostSet(TextureHandle input, const char* debugName);   // Layout_Post 순서: b0, b4, t13, s6, t20
	// 이번 프레임의 환경 바인딩. Scene::environment 의 이름들을 ResolveEnvironment 가 핸들과 셰이더 파라미터로 바꿈.
	struct EnvironmentBinding
	{
		TextureHandle skybox;          // t9  (없으면 검은 큐브)
		TextureHandle irradiance;      // t10 (IBL 확산 조도. 없으면 스카이박스 → 반사 큐브 → 검은 큐브 순으로 대체)
		TextureHandle specular;        // t11 (IBL 반사 프리필터. 없으면 스카이박스 → 검은 큐브)
		TextureHandle brdfLut;         // t12 (없으면 흰색 2D, 셰이더는 해석적 근사를 씀)
		uint32_t specularMips = 1;     // t11 의 밉 수 — 거칠기 → 밉 매핑의 끝
		float irradianceLod = 0.0f;    // t10 을 읽을 밉. 진짜 조도 큐브면 0, 스카이박스로 대체하면 끝에서 두 번째 밉(면당 2×2)
		bool hasEnvironment = false;   // 거짓이면 셰이더가 ambientColor 를 씀
		bool hasBrdfLut = false;
		float iblIntensity = 1.0f;
		DirectX::XMFLOAT3 iblTint = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
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
	// b1: m_objects[object] 의 미리 계산한 행렬을 올림 (+ 스킨이면 b7). withHistory = 속도용 지난 World·팔레트(b13)도 올림 —
	// 메인·미리보기 패스는 참, 그림자 패스는 거짓 (그림자는 속도를 쓰지 않음. prevWorld 는 world 로 채움).
	void UploadObjectConstants(uint32_t object, bool withHistory);
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
	uint32_t AddShadowView(const DirectX::XMMATRIX& viewProj, uint32_t x, uint32_t y, uint32_t size, const DirectX::XMFLOAT4& params, uint32_t lightIndex, uint32_t atlas = 0);
	void UploadShadowConstants();   // b6 전체 + b5 단위 행렬. 그림자가 꺼진 프레임에도 부름 (D3D12 의 Dynamic CB 갱신 검사)

	const GpuMesh* GetOrCreateGpuMesh(const Mesh& mesh);
	const GpuMaterial* GetOrCreateGpuMaterial(const Material& material, const Scene* scene);
	// mirrored: 월드 행렬의 행렬식이 음수(반사)인 오브젝트. 감기 순서가 뒤집히므로 앞면
	// 정의(frontCounterClockwise)를 반대로 둠.
	// PSO 핸들은 "Desc 를 가르는 몇 비트"를 키로 여기서 한 번 더 캐시함 — 드로우마다 Desc 전체를 복사·해시해
	// Device 캐시를 찾지 않음. 셰이더 핫리로드(BuildPassDescs)가 비움.
	// ocean: Gerstner 바다 드로우 (Pass_Ocean·Pass_GBufferOcean 에서 시작, 키 비트 12). 스킨 정점 포맷이면 무시함.
	PipelineHandle GetPipelineFor(const Material& material, VertexFormat vertexFormat, bool mirrored = false, bool ocean = false);
	PipelineHandle GetShadowPipelineFor(const Material& material, VertexFormat vertexFormat, bool mirrored = false);
	PipelineHandle GetSkyPipeline();   // 정점 속성 0개, 깊이 LessEqual + 쓰기 끔, 컬링 없음
	PipelineHandle GetTonemapPipeline();
	SamplerHandle GetSampler(SamplerPreset preset) const;

private:
	RHI::Device* m_device = nullptr;

	ShaderHandle m_shaders[Shader_Count];
	BufferHandle m_perFrameCB;
	BufferHandle m_perObjectCB;
	BufferHandle m_skinCB;                            // b7 Skin (스키닝 조인트 팔레트). 스키닝 드로우마다 올림
	BufferHandle m_prevSkinCB;                        // b13 PrevSkin (지난 프레임 팔레트). 메인·미리보기의 스키닝 드로우마다 올림
	std::vector<DirectX::XMFLOAT4X4> m_skinScratch;   // 전치한 팔레트를 만드는 임시 배열 (kMaxSkinJoints)
	BufferHandle m_lightCB;                           // b2 Lights (주변광, 조명 수)
	BufferHandle m_lightBuffer;                       // t15 조명 구조체 버퍼 (GpuLight × m_lightCapacity). Dynamic
	uint32_t m_lightCapacity = 0;                     // 원소 수. 모자라면 두 배로 다시 만듦
	std::vector<GpuLight> m_gpuLights;                // 이번 프레임에 올릴 조명 (BuildGpuLights)
	BufferHandle m_postParamsCB;                      // b4 PostParams
	// 블러 가우시안 가중치 (float × (kMaxBlurRadius + 1), w[i] = exp(−i²/2σ²), i ≥ 0 반쪽만). 컴퓨트 패스가 블러마다
	// 쓰고 PS·CS 블러가 t20 으로 읽음 — 두 경로가 같은 가중치를 읽음 (각자 계산하던 때는 D3D11 에서 최하위 비트가 달랐음).
	// 모든 후처리 셋이 t20 에 꽂으므로 프레임마다 처음에 ShaderResource 로 전이함 (버퍼 상태는 프레임 끝에 Common).
	BufferHandle m_blurWeights;
	ResourceSetHandle m_blurWeightsSet;   // Layout_BlurWeights: b4, u0 = m_blurWeights
	BufferHandle m_shadowPassCB;          // b5 ShadowPass (뷰마다 올림)
	BufferHandle m_shadowsCB;             // b6 Shadows (프레임에 한 번)

	// 갱신 빈도별 레이아웃 (LayoutId 순서). 선언 사본(m_layoutDescs)은 패스별 바인딩 대조가 씀.
	//   Layout_Frame     b0 (VS|PS), b2 (PS), t8 그림자 맵, s4 비교 샘플러, t9 스카이박스, s5, t10~t12 IBL (모두 PS), b5 그림자 패스 행렬 (VS), b6 그림자 뷰 배열 (PS), t14 CSM, t15 조명 구조체 버퍼, t30·t31 클러스터 (모두 PS)
	//   Layout_Object    b1, b7 스킨 팔레트, b13 지난 팔레트 (VS), b9 바다 (VS|PS)
	//   Layout_Material  b3 (VS|PS), t0 알베도, t1 노멀, t2 금속성/거칠기, t3 AO, t4 발광, t5 반사 세기, t6 반사 색, t16~t18 클리어코트, t19 투과, s0 (모두 PS)
	//   Layout_Post      b0, b4, t13, s6, t20 가중치 (모두 PS)
	//   Layout_BlurCompute b4, t13 입력, u0 출력, t20 가중치 (모두 CS)
	//   Layout_BlurWeights b4, u0 가중치 버퍼 (모두 CS)
	//   나머지 (파티클·G-buffer·SSAO·시간·클러스터 비닝·UI) 는 CreateLayouts 에 적음. 모두 Layout_Count = 19 개.
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
	PipelineStateDesc m_postAddDesc;   // Pass_Post + 가산 블렌드
	// PSO 핸들 캐시 (작은 키 → 핸들). 키는 Get*PipelineFor 가 만듦.
	std::unordered_map<uint32_t, PipelineHandle> m_mainPipelines;
	std::unordered_map<uint32_t, PipelineHandle> m_shadowPipelines;
	std::unordered_map<uint32_t, PipelineHandle> m_gbufferPipelines;   // GetGBufferPipelineFor (키는 메인과 같은 모양)
	PipelineHandle m_deferredLightPipeline;
	PipelineHandle m_ssaoPipelines[2];   // [blur]

	// ---- 클러스터 (3b) ----
	static constexpr uint32_t kInitialClusterLightRefs = 16384;   // t31 처음 원소 수 (64KB)
	// 상한 (4MB). 넘치면 버리고 Stats::clusterOverflow. 버퍼는 필요한 만큼만 두 배씩 키우고 업로드도 실제 개수만큼이라, 상한을 올려도
	// 실제 쌍이 적은 씬의 비용은 그대로임. 262,144 에서는 Intel Sponza 촛불 1만 개(range 2 m)가 시점에 따라 약 46~49만 쌍이라 넘쳤음 (2026-11-21)
	static constexpr uint32_t kMaxClusterLightRefs = 1048576;
	static constexpr uint32_t kClusterViews = 2;   // 0 메인, 1 카메라 미리보기
	BufferHandle m_clusterGridBuffer;              // t30 Dynamic, uint2 × kClusterCount
	BufferHandle m_clusterIndexBuffer;             // t31 Dynamic, uint × m_clusterIndexCapacity
	uint32_t m_clusterIndexCapacity = 0;
	std::vector<uint32_t> m_clusterGridScratch[kClusterViews];    // (시작, 개수) 쌍을 펼친 것 — 길이 2 × kClusterCount
	std::vector<uint32_t> m_clusterIndexScratch[kClusterViews];   // 조명 번호 목록
	bool m_clusterBuilt[kClusterViews] = {};                      // 이번 프레임에 그 뷰를 비닝했는가
	// GPU 비닝 (RendererClusterBin.cpp). t30·t31 은 CPU 경로와 다른 Default + UAV 버퍼 두 개 — 프레임 셋이 경로에 따라 둘 중
	// 하나를 꽂음 (m_frameSetClusterGpu). t31 은 처음부터 상한 크기 (GPU 는 개수를 세기 전에 버퍼가 있어야 함).
	BufferHandle m_clusterBinCB[kClusterViews];   // b13 Dynamic, 뷰마다
	BufferHandle m_lightBoundsBuffer;             // t7 Dynamic, float4 × m_clusterGpuLightCapacity (월드 경계 구)
	BufferHandle m_clusterViewBounds;             // u0 Default, float4 × 용량 (뷰 공간 경계 구)
	BufferHandle m_clusterTileCounts;             // u1 Default, uint × kClusterTileCount
	BufferHandle m_clusterTileMasks;              // u2 Default, uint × kClusterTileCount × ⌈용량 / 32⌉ (타일마다 고정 구간의 비트마스크)
	BufferHandle m_clusterGridGpu;                // u3 / t30 Default, uint2 × kClusterCount
	BufferHandle m_clusterIndexGpu;               // u4 / t31 Default, uint × kMaxClusterLightRefs
	BufferHandle m_clusterStatsBuffer;            // u5 Default, uint × kClusterStatCount
	// 통계 리드백 링. kFrameCount 보다 넉넉히 둠 — D3D11 은 드라이버가 몇 프레임을 쌓아 두므로 2 프레임 전 복사가 아직 안 끝났을
	// 수 있음. 읽지 못한 슬롯은 덮어쓰지 않아 (그 프레임은 복사를 건너뜀), 읽은 값은 항상 한 프레임의 온전한 통계임.
	static constexpr uint32_t kClusterStatsSlots = 4;
	BufferHandle m_clusterStatsReadback[kClusterStatsSlots];     // Staging
	bool m_clusterStatsPending[kClusterStatsSlots] = {};         // 그 슬롯에 아직 읽지 않은 복사가 있음
	uint32_t m_clusterStatsCursor = 0;                           // 다음에 복사할 슬롯 (메인 뷰 디스패치마다 하나씩)
	uint64_t m_clusterStatsCopyFrame[kClusterStatsSlots] = {};   // 그 슬롯에 복사한 GPU 비닝 프레임 번호
	uint64_t m_clusterBinFrame = 0;                              // GPU 비닝을 한 프레임 수 (ReadGpuClusterStats 가 셈)
	static constexpr uint32_t kClusterStatValues = 8;            // = ShaderConstants.h 의 kClusterStatCount (RendererClusterBin.cpp 가 static_assert)
	uint32_t m_clusterGpuStats[kClusterStatValues] = {};         // 마지막으로 읽은 통계 (읽지 못한 프레임에도 이 값을 보여 줌)
	ResourceSetHandle m_clusterBinSet[kClusterViews];
	uint32_t m_clusterGpuLightCapacity = 0;               // 위 용량 버퍼들을 만든 조명 수 (= 그때의 m_lightCapacity)
	bool m_clusterGpuFailed = false;                      // 버퍼 생성 실패 — 다시 시도하지 않고 CPU 경로
	bool m_clusterGpuThisFrame = false;                   // 이번 프레임 GPU 경로
	bool m_frameSetClusterGpu = false;                    // 프레임 셋 t30·t31 이 GPU 버퍼를 꽂고 있음
	std::vector<DirectX::XMFLOAT4> m_lightBounds;         // 조명마다의 경계 구 (ComputeLightBounds, 이번 프레임)
	std::vector<uint8_t> m_lightVisible;                  // 3a: 메인 카메라 절두체 안 (방향광은 항상 1)
	std::vector<uint32_t> m_shadowOrder;                  // 3a: 그림자 후보 조명 번호, 점수 높은 순
	std::unordered_map<uint32_t, bool> m_shadowHistory;   // 3a: 지난 프레임에 그림자를 받은 씬 조명 번호 (깜빡임 억제용 가산점)
	std::vector<uint32_t> m_lightSceneIndex;              // 정렬 뒤 m_gpuLights[i] 가 씬의 몇 번째 조명이었는지

	// ---- SSAO (5단계) ----
	BufferHandle m_ssaoCB;                         // b12 Dynamic (SsaoConstants)
	std::vector<DirectX::XMFLOAT4> m_ssaoKernel;   // 반구 커널 (sampleCount 개). 샘플 수가 바뀔 때만 다시 만듦 (시드 고정이라 결정적)
	SsaoSettings m_ssaoSettings;                   // 이번 프레임 (씬 환경에서 복사)
	PipelineHandle m_skyPipeline;
	PipelineHandle m_debugLinePipeline;
	PipelineHandle m_particlePipeline;
	BufferHandle m_particleCollisionCB;                     // Dynamic b9 (ParticleCollisionConstants). 뷰마다의 충돌 셋이 가리킴
	BufferHandle m_particleSortViewCB[kMaxParticleViews];   // Dynamic b10, 뷰마다 (알파 이미터가 있는 프레임에 올림)
	struct ParticleSortStep
	{
		BufferHandle constants;   // Default b11 (ParticleSortStepConstants), 초기 데이터로 만들고 바꾸지 않음
		ResourceSetHandle set;    // Layout_ParticleSortStep
	};
	std::vector<ParticleSortStep> m_particleSortSteps;   // k = 2, 4, … 마다 j = k/2 … 1 순서. 큰 N 은 작은 N 의 단계 목록을 앞에 그대로 가짐
	uint32_t m_particleSortStepCount = 0;                // m_particleSortSteps 가 정렬할 수 있는 최대 길이

	// 이미터 하나의 GPU 상태. 키 = 오브젝트 ID × 16 + 그 오브젝트 안에서 몇 번째 이미터인지 (undo·씬 로드가 오브젝트를
	// 다시 만들어도 ID 가 같으면 이어서 씀). 이번 프레임에 보이지 않은 키는 UpdateParticleEmitters 가 지움.
	struct GpuEmitter
	{
		BufferHandle particles;          // Default, SRV + UAV, stride kParticleStride, capacity 개
		BufferHandle deadList;           // UAV, uint × capacity (비어 있는 슬롯 번호)
		BufferHandle aliveLists[2];      // SRV + UAV, uint × capacity. 프레임마다 입력·출력이 바뀜
		BufferHandle counters;           // UAV, uint × kParticleCounterCount
		BufferHandle args;               // Raw + UAV + IndirectArgs, kParticleArgsSize 바이트
		BufferHandle constants;          // Dynamic b8 (ParticleEmitterConstants)
		ResourceSetHandle simSets[2];    // Layout_ParticleSim: b8, u0~u5. [p] = 입력 aliveLists[p], 출력 aliveLists[1 - p]
		ResourceSetHandle stepSets[2];   // Layout_ParticleStep: simSets 에서 u5 (인자 버퍼) 만 뺀 것 — 간접 디스패치용
		ResourceSetHandle drawSets[2];   // Layout_ParticleDraw: b8, t21, t22 = aliveLists[1 - p] (그 프레임의 출력)
		// 알파 이미터 (blendMode 1) 만: 뷰마다 정렬 결과. 길이 = ParticleSortCount(capacity)
		BufferHandle sortKeys[kMaxParticleViews];              // UAV, float
		BufferHandle sortIndices[kMaxParticleViews];           // SRV + UAV, uint. 그리기 셋의 t22
		ResourceSetHandle sortSets[kMaxParticleViews][2];      // Layout_ParticleSort. [뷰][parity] (t22 입력 = 그 프레임의 출력 생존 목록)
		ResourceSetHandle drawSortedSets[kMaxParticleViews];   // Layout_ParticleDraw, t22 = sortIndices[뷰]
		bool sorted = false;                                   // 이번 프레임에 정렬해서 그림 (blendMode 1 이고 정렬 리소스가 있음)
		uint32_t capacity = 0;
		uint32_t parity = 0;       // 이번 프레임에 쓸 simSets / drawSets 번호
		uint32_t spawnCount = 0;   // 이번 프레임에 원하는 생성 수 (Emit 디스패치 크기)
		uint32_t seed = 0;         // 프레임마다 1 씩
		float spawnCarry = 0.0f;   // 정수로 떨어지지 않은 생성 수 (다음 프레임으로 넘김)
		bool needsReset = true;
		uint64_t lastSeenFrame = 0;
	};
	struct FrameEmitter
	{
		uint64_t key = 0;
		const ParticleEmitter* emitter = nullptr;   // 이번 프레임 동안만 유효 (씬이 소유)
		DirectX::XMFLOAT3 position = {};
		DirectX::XMFLOAT3 direction = {};
	};
	std::unordered_map<uint64_t, GpuEmitter> m_gpuEmitters;
	std::vector<FrameEmitter> m_frameEmitters;   // BuildDrawList 가 채움
	std::vector<uint64_t> m_activeEmitterKeys;   // 이번 프레임에 시뮬레이션·그리기할 키 (UpdateParticleEmitters 가 채움)
	float m_lastParticleTime = -1.0f;
	// 바다: 이번 프레임의 첫 번째 바다 (CollectOcean) 와 b9. 지난 위상은 time − dt 가 아니라 지난 프레임에 실제로 쓴 시간으로 만듦.
	struct FrameOcean
	{
		bool active = false;
		uint64_t objectId = 0;
		int waveCount = 0;
		OceanWave waves[Ocean::kMaxWaves];
		DirectX::XMFLOAT3 origin = {};   // 바다 오브젝트 월드 원점 (감쇠 기준)
		float fadeStart = 0.0f;          // 거리 감쇠 (Ocean::fadeStart·fadeEnd)
		float fadeEnd = 0.0f;
	};
	FrameOcean m_frameOcean;
	BufferHandle m_oceanCB;                           // b9 Dynamic (OceanConstants). 프레임에 한 번
	double m_oceanPrevTime = 0.0;                     // 지난 프레임에 바다가 쓴 시간 (초)
	uint64_t m_oceanPrevObject = 0;                   // 그 바다 오브젝트 id. 다르면 (바다가 바뀜) 지난 위상 = 이번 위상
	bool m_oceanHasPrev = false;                      // ResetTemporalHistory·바다가 없는 프레임에서 거짓
	bool m_oceanExtraWarned = false;                  // 두 번째 바다 경고 (한 번만)
	BufferHandle m_debugLineVB;                       // 동적 정점 버퍼 (DebugDraw::kMaxVertices 개)
	// 게임 UI (RendererUI.cpp)
	const UIDrawList* m_uiDrawList = nullptr;
	BufferHandle m_uiVB;                              // Dynamic. 모자라면 두 배로 다시 만듦 (VB·IB 는 셋에 넣지 않고 직접 묶음)
	BufferHandle m_uiIB;
	uint32_t m_uiVertexCapacity = 0;
	uint32_t m_uiIndexCapacity = 0;
	BufferHandle m_uiCB;                              // b4 UIConstants
	ResourceSetHandle m_uiCommonSet;                  // Layout_UICommon: b4, s0
	std::unordered_map<uint64_t, ResourceSetHandle> m_uiTextureSets;   // 키 = 텍스처 핸들 (index << 32 | generation)
	uint64_t m_uiTextureGeneration = 0;               // TextureCache 세대가 바뀌면 셋 캐시를 비움
	std::vector<ResourceSetHandle> m_uiCommandSets;   // 이번 프레임 명령마다 고른 텍스처 셋
	PipelineHandle m_uiPipeline;
	PipelineHandle m_uiWorldPipeline;
	TextureHandle m_uiAtlasTexture;                   // 글리프 아틀라스 (R8). 버전이 바뀌면 새로 만듦, 아니면 dirty 만 UpdateTexture
	uint64_t m_uiAtlasVersion = 0;
	bool m_uiReady = false;                           // 이번 프레임에 UI 버퍼를 올렸음
	uint32_t m_debugLineCount = 0;                    // 올린 정점 수 (0 이면 그리지 않음)
	PipelineHandle m_tonemapPipelines[2];             // [srgbOutput]
	PipelineHandle m_postPipelines[2];                // [additive]
	PipelineHandle m_temporalPipelines[Pass_Count];   // 시간 셋 풀스크린 패스 (VelocityDebug 등). FP16 출력 1장, 깊이 없음
	ComputePipelineHandle m_computePipelines[ComputePass_Count];
	bool m_computeTried[ComputePass_Count] = {};   // 한 번 만들어 보고 실패하면 (컴퓨트 미지원 백엔드) 매 프레임 다시 시도하지 않음
	RenderSettings m_settings;
	Stats m_stats;
	uint64_t m_frameNumber = 0;

	Material m_defaultMaterial;   // GameObject.material == nullptr 일 때

	// 샘플러 프리셋과 텍스처 캐시 (2D·큐브, 내장 흰색·평평한 노멀·검은 큐브)
	static constexpr uint32_t kSamplerPresetCount = 5;
	SamplerHandle m_samplers[kSamplerPresetCount];
	TextureCache m_textures;

	EnvironmentBinding m_environment;        // 이번 프레임 (ResolveEnvironment)
	PostProcessSettings m_postSettings;      // 이번 프레임 (ResolveEnvironment 가 씬의 환경에서 복사)
	ShadowSettings m_shadowSettings;         // 이번 프레임 (Render 가 맨 앞에서 씬의 환경에서 복사. 그림자 행렬과 b0 가 씀)
	EnvironmentBinding m_boundEnvironment;   // 지금 m_frameSet 에 꽂힌 것. 비우면 다음 UpdateFrameSet 이 셋을 다시 만듦

	// 그림자 맵 (D32_FLOAT, DSV + SRV) 과 비교 샘플러. 상태는 Barrier 로 전이함 (현재 상태는 RHI 가 추적).
	TextureHandle m_shadowMap;
	SamplerHandle m_shadowSampler;
	TextureHandle m_csmMap;   // 방향광 CSM 전용 아틀라스 (kCsmAtlasWidth × kCsmAtlasHeight, 같은 비교 샘플러)

	// 뷰 타깃. 메인: 에디터면 씬 뷰 크기(SetSceneTarget, color 있음), 게임 모드면 출력 크기(EnsureGameTarget,
	// color 없음 — 톤 패스가 백버퍼에 씀). 미리보기: 선택한 카메라 오브젝트의 시점 (SetPreviewTarget).
	ViewTarget m_mainView;
	ViewTarget m_previewView;
	uint32_t m_sceneWidth = 0;   // SetSceneTarget 이 요청한 씬 뷰 크기 (0 = 오프스크린 아님)
	uint32_t m_sceneHeight = 0;
	uint32_t m_outputWidth = 0;   // 게임 모드 출력(백버퍼) 크기 — SetOutputSize
	uint32_t m_outputHeight = 0;
	PostStageThumb m_stageThumbs[kPostStageCount];   // 단계별 썸네일 (RGBA8, 폭 384). 요청이 있을 때 처음 만듦
	uint32_t m_thumbWidth = 0;
	uint32_t m_thumbHeight = 0;
	bool m_postCaptureRequested = false;   // 창이 이번 프레임에 요청함 → 다음 Render 가 읽고 지움
	bool m_postCapture = false;            // 이번 Render 가 캡처함
	const Camera* m_previewCamera = nullptr;
	float m_frameDeltaTime = 1.0f / 60.0f;   // SetFrameDeltaTime
	uint64_t m_previewSourceId = 0;          // 이번 프레임 미리보기 카메라의 오브젝트 id (SetPreviewCamera)
	uint64_t m_previewLastSourceId = 0;      // 지난번 미리보기를 그린 오브젝트 id. 다르면 미리보기 뷰 히스토리 리셋
	bool m_shadowEnabledThisFrame = false;
	ShadowView m_shadowViews[MAX_SHADOW_VIEWS];
	uint32_t m_shadowViewCount = 0;
	uint32_t m_skippedShadowLights = 0;   // castShadows 인데 자리가 없어 그림자를 못 받은 조명 수
	uint32_t m_usedLocalTiles = 0;        // 이번 프레임에 쓴 지역광 타일 수 (0..kShadowLocalTileCount)
	// CSM (이번 프레임). 그림자는 메인 카메라 기준으로 한 번만 만들어서, 미리보기 패스에서도 PS 가 이 값으로
	// 캐스케이드를 고름.
	uint32_t m_cascadeCount = 0;              // 0 = CSM 아님
	DirectX::XMFLOAT4 m_cascadeSplits = {};   // 캐스케이드 i 가 끝나는 카메라 전방 거리 (없는 칸은 0)
	DirectX::XMFLOAT3 m_cascadeOrigin = {};   // 캐스케이드를 계산한 카메라의 위치·전방
	DirectX::XMFLOAT3 m_cascadeForward = {};
	DirectX::XMFLOAT3 m_cascadeCorners[4][8] = {};   // 에디터 오버레이용 (GetCascadeFrustumCorners)
	bool m_cullFrozen = false;          // SetCullFreeze
	CullFreezeParts m_cullFreezeParts;  // 고정 중 사본을 쓰는 항목
	Camera m_cullFreezeCamera;          // 고정 중일 때 켜 둔 항목을 계산하는 카메라 사본

	// Scene이 메시(shared_ptr)·재질(unique_ptr)을 힙에 따로 두므로 포인터가 안정적임. 그래서 키로 씀.
	std::unordered_map<const Mesh*, GpuMesh> m_gpuMeshes;
	std::unordered_map<const Material*, GpuMaterial> m_gpuMaterials;
	std::vector<DrawItem> m_drawList;
	std::vector<const DrawItem*> m_blendedScratch;   // DrawItems 가 뷰마다 거리순으로 다시 정렬하는 블렌딩 항목 (재사용 버퍼)
	std::vector<ObjectData> m_objects;               // 이번 프레임에 그릴 오브젝트 (DrawItem::object 가 가리킴). BuildDrawList 가 채움
	MotionHistory m_motion[2];                       // 속도 버퍼의 오브젝트 상태. [m_motionCurrent] = 이번 프레임, 나머지 = 지난 프레임
	uint32_t m_motionCurrent = 0;
	// BuildDrawList 의 계층 순회 스택. 프레임마다 재사용함. 칸 하나 = 자식이 있는 오브젝트 하나 + 그 월드 행렬 + 다음에 방문할 자식 번호.
	struct HierarchyFrame
	{
		const GameObject* node;
		size_t next;
		DirectX::XMFLOAT4X4 world;
	};
	std::vector<HierarchyFrame> m_hierarchyStack;

	// 핫리로드: 감시하는 소스 파일과 마지막 수정 시각
	struct WatchedFile
	{
		std::wstring path;
		uint64_t writeTime = 0;
	};
	std::vector<WatchedFile> m_watched;
	float m_nextWatchTime = 0.0f;
};

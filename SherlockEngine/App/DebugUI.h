#pragma once

class Scene;
class Renderer;
namespace RHI { class Device; }
class Camera;
struct ModelStats;
class Engine;

// ImGui 디버그 패널. 예전에는 Renderer::UpdateGUI가 조명 패널을 갖고 있었음(D3).
// 패널은 데이터를 "편집"할 뿐 "소유"하지는 않으므로 App 계층에 둠.
namespace DebugUI
{
	void DrawCameraPanel(const Camera& camera, float& moveSpeed);
	void DrawRenderSettingsPanel(Renderer& renderer, RHI::Device& device);
	void DrawShadowPanel(Renderer& renderer, RHI::Device& device);   // 6단계: 그림자 설정 + 그림자 맵 미리보기
	void DrawLightPanel(Scene& scene);                           // 6단계: 조명 8개 전부 편집, 추가/삭제
	// 2026-10-08: 씬 환경 (Scene::environment) — 스카이박스, IBL(확산 조도·반사 프리필터·BRDF LUT·세기), 노출·톤매핑.
	// 칸마다 이름 입력 + 콘텐츠 브라우저의 텍스처 드롭 (큐브 칸: .dds = 큐브 DDS 파일, 그 밖의 이미지 = 그 이미지가 든 폴더).
	void DrawEnvironmentPanel(Scene& scene);
	void DrawMaterialPanel(Scene& scene);
	// 9단계: 현재 씬 이름, 모델 통계 (메시·서브메시·정점·삼각형·16비트 인덱스·탄젠트 생성·로드 시간), 씬 전환 안내를 표시함.
	void DrawModelPanel(const char* sceneName, const ModelStats& stats, size_t sceneImages, size_t sceneObjects);

	// 10단계
	void DrawEnginePanel(Engine& engine);       // 백엔드·설정 파일·로그 파일·AssetManager·Time
	void DrawProfilerPanel();                   // CPU 구간 + GPU 타임스탬프 구간 (직전 완료 프레임)
	void DrawLogConsole(bool* open);            // 별도 창 "Console": 레벨 필터·검색·자동 스크롤·지우기
}

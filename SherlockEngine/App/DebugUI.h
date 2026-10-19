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
	// 2026-10-12: 좁은 패널 대응. ImGui::Begin/End 대신 쓰면 (1) 가로 스크롤바가 켜지고 (2) 위젯 폭이 라벨 폭을 뺀 만큼으로 잡혀
	// 슬라이더·콤보의 오른쪽 라벨이 잘리지 않으며 (3) 창이 너무 좁아지지 않음. 패널 안의 긴 문장은 TextWrapped 로 줄바꿈함.
	bool BeginPanel(const char* name, bool* open = nullptr);
	void EndPanel();
	// 가로로 이어 붙이는 위젯(체크박스·버튼 줄): 직전 위젯 오른쪽에 itemWidth 만큼 들어가면 SameLine, 아니면 다음 줄로 내림.
	void SameLineIfFits(float itemWidth);
	float CheckboxWidth(const char* label);
	float ButtonWidth(const char* label);        // Button·SmallButton 공통 (가로 패딩은 같음)
	float TextItemWidth(const char* text);

	void DrawCameraPanel(Camera& camera, float& moveSpeed);
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

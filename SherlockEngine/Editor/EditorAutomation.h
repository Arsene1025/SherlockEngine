#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Graphics/TextureImporter.h"
#include <DirectXMath.h>

// 자동 검증 (실행 인자). OS 입력을 주입하지 않고 에디터와 같은 코드 경로를 거침 — 스크린샷은 화면 캡처
// 대신 Device 리드백으로 찍음.
//   --exit-after=N        N 프레임 뒤 종료 (자동 모드 스위치: 창은 화면 밖, 포커스 없음)
//   --pick=u,v            5 프레임째 씬 뷰의 (u,v) 픽셀(0..1)로 Editor::Pick — 클릭 선택과 같은 코드
//   --set-position=x,y,z  8 프레임째 선택 오브젝트 위치 변경 (인스펙터 편집과 같은 효과)
//   --save=path           12 프레임째 씬 저장,   --load=path  16 프레임째 씬 로드
//   --dump-objects        20 프레임째 오브젝트 이름·위치를 로그로 출력
//   --screenshot=path     N−1 프레임째 백버퍼 PNG
//   --browse=relDir       2 프레임째 콘텐츠 브라우저 폴더 이동 (스크린샷용)
//   --drop=rel;x,y,z      6 프레임째 PlaceModel — 씬 뷰 드롭과 같은 코드 경로
//   --drop-uv=u,v         --drop 의 위치 대신 씬 뷰 (u,v) 광선의 히트점 (Editor::RaycastScene)
//   --drop-texture=rel    7 프레임째 선택 오브젝트 재질의 알베도에 텍스처 에셋 지정 (드롭과 같은 이름 규칙)
//   --reload-scripts=1    5 프레임째 ReloadScripts (DLL 내리고 다시 올림 — 게임 UI 이벤트 액션 등록 해제·재등록 확인)
//   --play=1              9 프레임째 StartPlay (▶ 와 같은 경로)
//   --stop-at=N           N 프레임째 StopPlay (스냅샷 복원)
//   --add-component=Name  10 프레임째 선택 오브젝트에 컴포넌트 추가 (Inspector 의 Add Component 와 같은 경로)
//   --compute-blur=1|0    시작할 때 가우시안 블러 경로 (1 = 컴퓨트 (기본), 0 = PS). --exit-after 없이도 동작. 게임 exe 도 같은 인자
//   --particle-step=<초>  파티클을 프레임마다 이 시간만큼 진행 (예: 0.0166667). 실제 시간이 아니라 프레임 수에만 의존하므로
//                         스크린샷이 실행마다 같음. --exit-after 없이도 동작. 게임 exe 도 같은 인자
//   --particle-sort=0     알파 파티클 (blendMode 1) 을 정렬하지 않고 그림 — 정렬 효과 비교용. 게임 exe 도 같은 인자
//   --deferred=1|0        디퍼드 경로 (1 = 기본). 0 이면 전부 포워드 — 두 경로 비교용. 게임 exe 도 같은 인자
//   --clustered=1|0       클러스터드 라이팅 (1 = 기본). 0 이면 픽셀마다 조명 전부를 돎 — 결과가 같은지 비교용
//   --cluster-gpu=1|0     클러스터 비닝을 컴퓨트로 (1 = 기본). 0 이면 CPU 비닝 — 결과가 같은지 비교용. 게임 exe 도 같은 인자
//   --cluster-tiles=1|0   GPU 비닝의 타일 단계 (1 = 기본, 2단계 비닝). 0 이면 상자마다 조명 전체 (1단계) — 비용 비교용
//   --avg-frames=N        종료 직전 N 프레임의 구간 시간(cpu:·gpu:) 평균을 "구간 평균 ms" 줄로 남김 (측정용)
//   --shadow-priority=1|0 그림자 타일을 화면 기여도 순으로 (1 = 기본). 0 이면 조명 번호 순 (예전 방식)
//   --ssao=1|0            씬 파일의 SSAO 켜짐 값을 덮어씀 (없으면 씬 값). 디퍼드 프레임에서만 효과가 있음
//   --freeze-culling=N    N 프레임째 에디터 카메라로 컬링 고정 (Renderer::SetCullFreeze). --cam-look 은 N+1 프레임째
//   --freeze-parts=col    --freeze-culling 의 항목: c 캐스케이드, o 오브젝트, l 조명 (없으면 셋 다)
//   --freeze-cascades=N   N 프레임째 캐스케이드만 고정 (예전 동작, docs/study/csm-study.html)
struct EditorAutomation
{
	bool active = false;
	uint64_t exitAfter = 0;
	uint32_t avgFrames = 0;   // --avg-frames=N. 종료 직전 N 프레임의 Profiler 구간 시간 평균을 따로 로그 (한 프레임 샘플은 흔들림)
	bool pick = false;
	float pickU = 0.5f, pickV = 0.5f;
	std::string selectName;   // --select=<오브젝트 이름>. 이름이 같은 첫 오브젝트를 선택함 (카메라 미리보기 검증 등)
	bool setPosition = false;
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	std::wstring savePath, loadPath, screenshotPath;
	bool dumpObjects = false;
	std::string texSettingName;
	TextureImportSettings texSetting;   // --tex-setting=이름;압축0|1;밉;최대크기 (12 프레임째 임포트 설정 변경 + 그 텍스처만 다시 읽기. Inspector 의 Apply 와 같은 경로)
	int debugBones = 0;                 // --debug-bones=N (2 프레임째 본 디버그 표시)
	int debugView = 0;                  // --debug-view=N (2 프레임째 Render Settings 의 디버그 뷰)
	bool lightIconsSelected = false;    // --light-icons=selected (2 프레임째 Gizmo 메뉴의 Selected light only)
	uint64_t freezeCascadesAt = 0;      // --freeze-cascades=N (N 프레임째 에디터 카메라로 캐스케이드만 고정 — 예전 동작. Renderer::SetCullFreeze)
	uint64_t freezeCullingAt = 0;       // --freeze-culling=N (N 프레임째 에디터 카메라로 컬링 고정, 항목은 --freeze-parts. 둘 다 주면 이쪽)
	bool freezeParts[3] = { true, true, true };   // --freeze-parts=col 중 고를 글자 (c 캐스케이드, o 오브젝트, l 조명). 없으면 셋 다
	bool camLook = false;
	DirectX::XMFLOAT3 camEye = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), camTarget = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);   // --cam-look=ex,ey,ez;tx,ty,tz (고정 다음 프레임, 고정이 없으면 3 프레임째 에디터 카메라 이동)
	bool newScene = false;                                                                                             // --new-scene=1 (3 프레임째 바닥만 있는 새 씬)
	int addPrimitive = -1;                                                                                             // --add=0..4 (4 프레임째 오브젝트 추가, 4 는 카메라 오브젝트)
	std::wstring browseDir;
	std::wstring dropPath;
	DirectX::XMFLOAT3 dropPosition = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	bool dropUv = false;
	float dropU = 0.5f, dropV = 0.5f;
	std::wstring dropTexture;
	bool play = false;
	bool reloadScripts = false;
	uint64_t stopAt = 0;
	std::string addComponent;
	int addUI = -1;   // --add-ui=0|1: Hierarchy "+ UI" 의 Canvas·Image (선택한 오브젝트 기준, AddUIElement)
	std::string newScript;   // --new-script=Name (2 프레임째 ScriptCreator::Create — Inspector 의 New Script 와 같은 경로, 열지는 않음)
	std::string buildGame;   // --build-game=Name (3 프레임째 GameBuilder::Build — 에셋 전부, 폴더 열지 않음, 시작 씬 = --build-scene 또는 프로젝트 시작 씬)
	std::string buildScene;
	std::string newProject;      // --new-project=Name (2 프레임째 CreateProject in <repo>\Projects\, 11-E)
	bool buildProject = false;   // --build-project=1 (4 프레임째 BuildScripts — 프로젝트 스크립트 DLL 을 MSBuild 로 빌드하고 다시 올림)
	bool launcher = false;       // --launcher=1 (2 프레임째 File > Projects 런처 팝업을 엶 — 문서 스크린샷용)
	bool setRotation = false;
	DirectX::XMFLOAT3 rotationDeg = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);   // --set-rotation=p,y,r (도, 8 프레임째 선택 오브젝트, 11-F)
	std::string parentChild, parentName;                                   // --set-parent=자식;부모 (7 프레임째 재부모화, 부모가 비면 루트로, 11-F)
	// 되돌리기·프리팹. 입력을 주입하지 못하므로 같은 코드 경로(RunCommand / UndoRedo / SavePrefabOf …)를 직접 부름
	//   --delete=이름          6 프레임째 그 오브젝트 삭제 (Delete 키와 같은 DeleteObject 를 RunCommand 로)
	//   --duplicate=이름       11 프레임째 그 오브젝트 복제 (Ctrl+D)
	//   --make-prefab=이름     11 프레임째 그 오브젝트(와 자식)를 Assets\Prefabs\<이름>.prefab 로 저장
	//   --instantiate=rel;x,y,z  11 프레임째 프리팹을 놓음 (씬 뷰 드롭과 같은 InstantiatePrefabAt)
	//   --undo=N / --redo=N    14 / 15 프레임째 Ctrl+Z / Ctrl+Y 를 N 번
	//   --save-after=path      18 프레임째 씬을 한 번 더 저장 (--save 는 12 프레임째 — 두 파일을 비교해 되돌리기를 검증)
	std::string deleteName, duplicateName, makePrefabName;
	std::wstring instantiatePath;
	DirectX::XMFLOAT3 instantiatePosition = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	int undoCount = 0, redoCount = 0;
	std::wstring saveAfterPath;
	//   --gesture=1            --set-position 을 RunCommand 가 아니라 "위젯을 끄는 중" 으로 흉내 냄: 8 프레임째 입력 진행 중(busy)으로 두고 값을 바꾼 뒤
	//                          11 프레임째 끝냄 → TrackHistory/EditorHistory::UpdateGesture 경로가 한
	//                          단계를 만드는지 확인
	bool gesture = false;
	// 움직임 (속도 버퍼·모션 블러·TAA 검증, docs/plan/velocity-motionblur-taa-plan.html). 프레임마다 고정량이라 실행마다 같은 화면.
	//   --cam-orbit=도          에디터 카메라를 프레임마다 그 각도만큼 수직축으로 돌림 (제자리 yaw)
	//   --spin=이름,도          편집 모드에서 그 오브젝트를 프레임마다 y 축으로 그 각도만큼 돌림 (되돌리기 기록 없음)
	//   --cam-move=dx,dy,dz     에디터 카메라를 프레임마다 월드 벡터만큼 옮김
	//   --move=이름,dx,dy,dz    그 오브젝트를 프레임마다 부모 축 벡터만큼 옮김. --cam-move 와 같은 벡터면 "카메라를 따라가는 오브젝트"
	//   --event-frame=N         한 번만 일어나는 동작(--cam-look, --select, --duplicate, --instantiate, --undo, --load)을 N 프레임째에 검.
	//                           동작과 스크린샷 요청이 같은 OnUpdate 에서 렌더 전에 실행되므로 N = exit-after − 1 이면 스크린샷이
	//                           "사건 직후 첫 프레임"을 담음
	float camOrbitDeg = 0.0f;
	std::string spinName;
	float spinDeg = 0.0f;
	bool camMove = false;
	DirectX::XMFLOAT3 camMoveDelta = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	std::string moveName;
	DirectX::XMFLOAT3 moveDelta = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	uint64_t eventFrame = 0;
	// 게임 UI 입력 (docs/plan/ui-canvas-plan.html 4장). Input::SetSimulated 로 가짜 입력 — 창 위 실제 커서와 상관없음.
	// 좌표는 씬 뷰 픽셀 (씬 이미지 왼쪽 위 기준). 여러 개는 ';' 로 (예: --key=Down@40;Enter@50)
	//   --ui-click=x,y@N          N 프레임 누르고 N+1 뗌
	//   --mouse=x,y@N             N 프레임 포인터만 옮김
	//   --ui-drag=x0,y0,x1,y1@N:F N 누름, N+1..N+F 사이를 고르게 옮김, N+F+1 뗌. 끝에 :r 이면 오른쪽 버튼
	//   --ui-wheel=x,y,d@N        N 프레임 포인터를 옮기고 휠 d 칸 (+ 위)
	//   --key=이름|VK@N           N 누름, N+1 뗌. 이름: Up Down Left Right Enter Space Esc Tab A..Z
	//   --pad=이름@N              N 누름, N+1 뗌. 이름: A B X Y Up Down Left Right Start Back StickUp StickDown StickLeft StickRight
	// 에디터 Rect 툴 (7단계). 가짜 포인터는 Editor::SetToolPointer (씬 뷰 픽셀)
	//   --tool=rect               2 프레임째 Rect 툴
	//   --tool-click=x,y@N        N 누름, N+1 뗌 (씬 뷰 클릭 선택)
	//   --tool-drag=x0,y0,x1,y1@N:F[:sac]  Rect 툴 드래그 (s Shift, a Alt, c Ctrl). 뗀 프레임에 선택 RectTransform 을 로그
	//   --log-rect=이름@N         N 프레임에 그 오브젝트 RectTransform 값과 화면 사각형을 로그
	struct InputEvent
	{
		enum Kind { Click, Mouse, Drag, Wheel, Key, Pad, ToolClick, ToolDrag, LogRect } kind = Click;
		uint64_t frame = 0;
		uint32_t frames = 0;
		float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f, wheel = 0.0f;
		uint32_t vk = 0;
		uint16_t pad = 0;
		float stickX = 0.0f, stickY = 0.0f;
		bool shift = false, alt = false, ctrl = false;
		bool right = false;   // --ui-drag 의 :r — 오른쪽 버튼 (카메라 보기 가로채기 검사)
		std::string name;
	};
	std::vector<InputEvent> inputEvents;
	bool rectTool = false;
};

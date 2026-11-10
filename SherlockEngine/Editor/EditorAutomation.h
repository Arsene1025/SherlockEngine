#pragma once
#include <cstdint>
#include <string>
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
//   --play=1              9 프레임째 StartPlay (▶ 와 같은 경로)
//   --stop-at=N           N 프레임째 StopPlay (스냅샷 복원)
//   --add-component=Name  10 프레임째 선택 오브젝트에 컴포넌트 추가 (Inspector 의 Add Component 와 같은 경로)
struct EditorAutomation
{
	bool active = false;
	uint64_t exitAfter = 0;
	bool pick = false; float pickU = 0.5f, pickV = 0.5f;
	std::string selectName;   // --select=<오브젝트 이름>. 이름이 같은 첫 오브젝트를 선택함 (카메라 미리보기 검증 등)
	bool setPosition = false; DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	std::wstring savePath, loadPath, screenshotPath;
	bool dumpObjects = false;
	int debugView = 0;          // --debug-view=N (2 프레임째 Render Settings 의 디버그 뷰)
	uint64_t freezeCascadesAt = 0;   // --freeze-cascades=N (N 프레임째 에디터 카메라로 캐스케이드 고정. Renderer::SetCascadeFreeze)
	bool camLook = false; DirectX::XMFLOAT3 camEye = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), camTarget = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);   // --cam-look=ex,ey,ez;tx,ty,tz (고정 다음 프레임, 고정이 없으면 3 프레임째 에디터 카메라 이동)
	bool newScene = false;      // --new-scene=1 (3 프레임째 바닥만 있는 새 씬)
	int addPrimitive = -1;      // --add=0..4 (4 프레임째 오브젝트 추가, 4 는 카메라 오브젝트)
	std::wstring browseDir;
	std::wstring dropPath; DirectX::XMFLOAT3 dropPosition = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	bool dropUv = false; float dropU = 0.5f, dropV = 0.5f;
	std::wstring dropTexture;
	bool play = false; uint64_t stopAt = 0; std::string addComponent;
	std::string newScript;      // --new-script=Name (2 프레임째 ScriptCreator::Create — Inspector 의 New Script 와 같은 경로, 열지는 않음)
	std::string buildGame;      // --build-game=Name (3 프레임째 GameBuilder::Build — 에셋 전부, 폴더 열지 않음, 시작 씬 = --build-scene 또는 프로젝트 시작 씬)
	std::string buildScene;
	std::string newProject;     // --new-project=Name (2 프레임째 CreateProject in <repo>\Projects\, 11-E)
	bool buildProject = false;  // --build-project=1 (4 프레임째 BuildScripts — 프로젝트 스크립트 DLL 을 MSBuild 로 빌드하고 다시 올림)
	bool launcher = false;      // --launcher=1 (2 프레임째 File > Projects 런처 팝업을 엶 — 문서 스크린샷용)
	bool setRotation = false; DirectX::XMFLOAT3 rotationDeg = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);   // --set-rotation=p,y,r (도, 8 프레임째 선택 오브젝트, 11-F)
	std::string parentChild, parentName;   // --set-parent=자식;부모 (7 프레임째 재부모화, 부모가 비면 루트로, 11-F)
	// 되돌리기·프리팹. 입력을 주입하지 못하므로 같은 코드 경로(RunCommand / UndoRedo / SavePrefabOf …)를 직접 부름
	//   --delete=이름          6 프레임째 그 오브젝트 삭제 (Delete 키와 같은 DeleteObject 를 RunCommand 로)
	//   --duplicate=이름       11 프레임째 그 오브젝트 복제 (Ctrl+D)
	//   --make-prefab=이름     11 프레임째 그 오브젝트(와 자식)를 Assets\Prefabs\<이름>.prefab 로 저장
	//   --instantiate=rel;x,y,z  11 프레임째 프리팹을 놓음 (씬 뷰 드롭과 같은 InstantiatePrefabAt)
	//   --undo=N / --redo=N    14 / 15 프레임째 Ctrl+Z / Ctrl+Y 를 N 번
	//   --save-after=path      18 프레임째 씬을 한 번 더 저장 (--save 는 12 프레임째 — 두 파일을 비교해 되돌리기를 검증)
	std::string deleteName, duplicateName, makePrefabName;
	std::wstring instantiatePath; DirectX::XMFLOAT3 instantiatePosition = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
	int undoCount = 0, redoCount = 0;
	std::wstring saveAfterPath;
	//   --gesture=1            --set-position 을 RunCommand 가 아니라 "위젯을 끄는 중" 으로 흉내 냄: 8 프레임째 입력 진행 중(busy)으로 두고 값을 바꾼 뒤
	//                          11 프레임째 끝냄 → TrackHistory/EditorHistory::UpdateGesture 경로가 한
	//                          단계를 만드는지 확인
	bool gesture = false;
};

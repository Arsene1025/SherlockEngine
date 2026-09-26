#pragma once
#include "App/AppBase.h"
#include "Graphics/Model.h"   // ModelStats
#include "App/Editor.h"

// 테스트 앱 (10단계부터 "엔진 위의 앱"). 씬 구성·카메라 컨트롤러·단축키·패널만 가짐.
// Device·Renderer·Scene·Input·Time 은 GetEngine() 으로 접근함. 렌더 순서는 Engine 이 정함.
class TestApp : public AppBase
{
public:
    // 9단계: 씬 세 가지. F11 로 순환하며, 설정 engine.scene 또는 실행 인자 --scene=demo|helmet|sponza 로 고름.
    enum class SceneMode : uint8_t
    {
        Demo,       // 3~6단계의 프리미티브 씬 (애니메이션)
        Helmet,     // Khronos DamagedHelmet (.glb, 텍스처 내장, 탄젠트 없음 → 생성)
        Sponza,     // Khronos Sponza (.gltf + .bin + 외부 이미지, 재질 25개, 알파 컷아웃)
        Count,
        File        // 11단계: JSON 에서 로드한 씬 (F11 순환에는 들어가지 않음)
    };

    TestApp();

protected:
    virtual bool OnInitialize() override;
    virtual void OnUpdate(float dt) override;
    virtual void OnFixedUpdate(float fixedDt) override;
    virtual void OnGUI() override;
    virtual void OnFocusLost() override;

private:
    void LoadSceneMode(SceneMode mode);
    void BuildDemoScene();
    bool BuildModelScene(SceneMode mode);
    bool SaveSceneFile(const std::wstring& path);   // 11단계
    bool LoadSceneFile(const std::wstring& path);
    void BuildEmptyScene();          // 바닥만 있는 새 씬 (File 모드)
    void AddPrimitive(int type);     // 0 구, 1 큐브, 2 원기둥, 3 평면, 4 카메라 오브젝트 (11-C)
    GameObject& AddCameraObject(const char* name);   // 11-C: 메시 없는 오브젝트 + CameraComponent 를 현재 에디터 시점에 만듦
    void DeleteObject(int index);
    // 11-B단계: 콘텐츠 브라우저 드롭. 모델(Assets 상대 경로)을 바닥면이 position.y 에 닿도록 position 에 놓음.
    void PlaceModel(const std::wstring& relativePath, const DirectX::XMFLOAT3& position);

    // 11-C단계: 재생 (에디터의 ▶). Play 는 씬+카메라를 JSON 문자열로 스냅샷한 뒤 Scene::BeginPlay 를 호출하고, Stop 은 스냅샷으로 되돌림.
    enum class PlayState : uint8_t { Editing, Playing, Paused };
    void StartPlay();
    void StopPlay();
    void TogglePause();
    void StepFrame();   // Paused 에서 고정 스텝 한 번

    // 11-E단계: 프로젝트
    bool OpenProjectAndScene(const std::wstring& pathOrDir);            // AppBase::OpenProject + 에디터 갱신 + 시작 씬 로드 + 최근 목록
    bool CreateProject(const std::wstring& parentDir, const std::string& name, std::string& error);   // 폴더·스크립트 예제·솔루션·Main.json
    void BuildAndLaunchProjectEditor();                                  // MSBuild <Name>Editor → 실행 → 종료
    void SyncProjectInfo();                                              // Editor::project 채우기 + 창 제목
    const wchar_t* GetWindowTitle() const override { return L"SherlockEditor"; }
    const char* GetSceneName() const;

    // WASD/QE 이동 + 우클릭 드래그 회전. ImGui가 입력을 쓰는 동안은 무시함.
    void UpdateCamera(float dt);
    void BeginLook();
    void EndLook();   // 여러 번 불러도 안전함 (포커스 상실·캡처 상실에서도 호출됨)

private:
    SceneMode m_sceneMode = SceneMode::Demo;
    Editor m_editor;               // 11단계
    Editor::Callbacks m_editorCallbacks;
    ModelStats m_modelStats;   // 모델 씬일 때의 로드 통계 (패널 표시)

    // 11단계: 자동 검증 (실행 인자). OS 입력을 주입하지 않고 에디터와 같은 코드 경로를 거침 — 스크린샷은 화면 캡처 대신 Device 리드백으로 찍음.
    //   --exit-after=N        N 프레임 뒤 종료 (자동 모드 스위치: 창은 화면 밖, 포커스 없음)
    //   --pick=u,v            5 프레임째 씬 뷰의 (u,v) 픽셀(0..1)로 Editor::Pick — 클릭 선택과 같은 코드
    //   --set-position=x,y,z  8 프레임째 선택 오브젝트 위치 변경 (인스펙터 편집과 같은 효과)
    //   --save=path           12 프레임째 씬 저장,   --load=path  16 프레임째 씬 로드
    //   --dump-objects        20 프레임째 오브젝트 이름·위치를 로그로 출력
    //   --screenshot=path     N−1 프레임째 백버퍼 PNG
    // 11-B단계:
    //   --browse=relDir       2 프레임째 콘텐츠 브라우저 폴더 이동 (스크린샷용)
    //   --drop=rel;x,y,z      6 프레임째 PlaceModel — 씬 뷰 드롭과 같은 코드 경로
    //   --drop-uv=u,v         --drop 의 위치 대신 씬 뷰 (u,v) 광선의 히트점 (Editor::RaycastScene)
    //   --drop-texture=rel    7 프레임째 선택 오브젝트 재질의 알베도에 텍스처 에셋 지정 (드롭과 같은 이름 규칙)
    // 11-C단계:
    //   --play=1              9 프레임째 StartPlay (▶ 와 같은 경로)
    //   --stop-at=N           N 프레임째 StopPlay (스냅샷 복원)
    //   --add-component=Name  10 프레임째 선택 오브젝트에 컴포넌트 추가 (Inspector 의 Add Component 와 같은 경로)
    struct Automation
    {
        bool active = false;
        uint64_t exitAfter = 0;
        bool pick = false; float pickU = 0.5f, pickV = 0.5f;
        bool setPosition = false; DirectX::XMFLOAT3 position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        std::wstring savePath, loadPath, screenshotPath;
        bool dumpObjects = false;
        int debugView = 0;          // --debug-view=N (2 프레임째 Render Settings 의 디버그 뷰)
        bool newScene = false;      // --new-scene=1 (3 프레임째 바닥만 있는 새 씬)
        int addPrimitive = -1;      // --add=0..4 (4 프레임째 오브젝트 추가, 4 는 카메라 오브젝트)
        std::wstring browseDir;     // 11-B단계
        std::wstring dropPath; DirectX::XMFLOAT3 dropPosition = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        bool dropUv = false; float dropU = 0.5f, dropV = 0.5f;
        std::wstring dropTexture;
        bool play = false; uint64_t stopAt = 0; std::string addComponent;   // 11-C단계
        std::string newScript;      // --new-script=Name (2 프레임째 ScriptCreator::Create — Inspector 의 New Script 와 같은 경로, 열지는 않음)
        std::string buildGame;      // --build-game=Name (3 프레임째 GameBuilder::Build — 에셋 전부, 폴더 열지 않음, 시작 씬 = --build-scene 또는 프로젝트 시작 씬)
        std::string buildScene;
        std::string newProject;     // --new-project=Name (2 프레임째 CreateProject in <repo>\Projects\, 11-E)
        bool buildProject = false;  // --build-project=1 (4 프레임째 프로젝트 솔루션의 에디터 타깃을 MSBuild 로 빌드 — 실행하지는 않음)
        bool launcher = false;      // --launcher=1 (2 프레임째 File > Projects 런처 팝업을 엶 — 문서 스크린샷용)
    } m_auto;
    void ParseAutomation();
    void RunAutomation();

    // 11-C단계: 데모 씬의 움직임은 스크립트(PlayerController·Rotator)와 엔진 컴포넌트(Rigidbody)가 맡고 재생 중에만 동작함.
    PlayState m_playState = PlayState::Editing;
    std::string m_playSnapshot;   // 재생 전 씬 JSON (SceneSerializer::SaveToString)
    float m_timeScale = 1.0f;
    bool m_stepOnce = false;

    // 6단계: F7 로 방향광 0 을 Y축 둘레로 회전시킴.
    bool m_lightOrbit = false;
    float m_lightAngle = 0.0f;

    // 10단계: 고정 스텝 검증용 카운터 (패널에 표시). OnFixedUpdate 호출 수 / 누적 시간.
    uint32_t m_fixedUpdates = 0;
    float m_fixedTime = 0.0f;

    // 마우스 회전 상태
    bool m_lookActive = false;
    POINT m_lookAnchorScreen = {};   // 드래그 시작 시 커서 위치(화면 좌표). 매 프레임 커서를 여기로 되돌림.
    float m_lookSensitivity = 0.0025f;   // 라디안 / 픽셀
    float m_moveSpeed = 10.0f;           // 단위 / 초. Shift로 4배.
};

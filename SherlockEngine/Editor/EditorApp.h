#pragma once
#include "Editor/EditorAutomation.h"
#include "App/AppBase.h"
#include "Graphics/Model.h"   // ModelStats
#include "Editor/Editor.h"
#include "Editor/EditorHistory.h"
#include <functional>

// 에디터 앱 (SherlockEditor.exe). "엔진 위의 앱"이며, GameApp(SherlockGame.exe)과 짝을 이룸. 씬 구성·카메라
// 컨트롤러·단축키·패널만 가짐. TestApp → EditorApp 으로 개명: 저장·불러오기 단위는 프로젝트(11-E)와 씬(11)이
// 맡으므로, 이 클래스는 에디터 실행 파일 그 자체임. Device·Renderer·Scene·Input·Time 은 GetEngine() 으로
// 접근함. 렌더 순서는 Engine 이 정함.
class EditorApp : public AppBase
{
public:
    // 내장 씬 세 가지. F11 로 순환하며, 설정 engine.scene 또는 실행 인자 --scene=demo|helmet|sponza 로 고름.
    // Helmet·Sponza 의 모델 파일은 SherlockTestScenes 프로젝트의 Assets\Models 에 있으므로 그 프로젝트를 연
    // 상태에서만 로드됨.
    enum class SceneMode : uint8_t
    {
        Demo,       // 프리미티브 씬 (애니메이션)
        Helmet,     // Khronos DamagedHelmet (.glb, 텍스처 내장, 탄젠트 없음 → 생성)
        Sponza,     // Khronos Sponza (.gltf + .bin + 외부 이미지, 재질 25개, 알파 컷아웃)
        Count,
        File        // JSON 에서 로드한 씬 (F11 순환에는 들어가지 않음)
    };

    EditorApp();

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
    bool SaveSceneFile(const std::wstring& path);
    bool LoadSceneFile(const std::wstring& path);
    void BuildEmptyScene();          // 바닥만 있는 새 씬 (File 모드)
    void AddPrimitive(int type);     // 0 구, 1 큐브, 2 원기둥, 3 평면, 4 카메라 오브젝트 (11-C)
    GameObject& AddCameraObject(const char* name);   // 11-C: 메시 없는 오브젝트 + CameraComponent 를 현재 에디터 시점에 만듦
    void DeleteObject(int index);
    // 명령 큐와 되돌리기 (Editor/EditorHistory.h).
    // 구조를 바꾸는 편집(추가·삭제·모델·프리팹·복제·씬 불러오기)은 위젯을 그리는 도중에 실행하지 않고 Enqueue
    // 로 큐에 넣음. OnGUI 가 Editor::Draw 를 끝낸 뒤 FlushCommands 가 순서대로 실행함 — 그때는 어떤 위젯도
    // 오브젝트 벡터를 순회하고 있지 않고, 결과는 같은 프레임의 렌더에 반영됨. 각 명령은 RunCommand 가 감싸
    // 실행 앞뒤의 씬 상태를 비교해 되돌리기 한 단계로 기록함.
    struct PendingCommand
    {
        std::string label;
        std::function<void()> run;
        bool record = true;   // false: 이력을 건드리는 명령(Undo/Redo 자신)이라 RunCommand 로 감싸지 않음
    };
    void Enqueue(const std::string& label, std::function<void()> run, bool record = true);
    void RunCommand(const std::string& label, const std::function<void()>& run);   // 큐를 거치지 않고 즉시 실행하되 이력에는 기록함 (자동 검증이 씀)
    void FlushCommands();
    void TrackHistory();                // OnGUI 끝: 위젯 편집(슬라이더·기즈모 …)이 끝난 프레임에 한 단계로 기록
    void UndoRedo(bool undo);
    void DuplicateObject(uint64_t id);
    void SavePrefabOf(uint64_t id);
    void InstantiatePrefabAt(const std::wstring& relativePath, const DirectX::XMFLOAT3& position);
    void ResetHistory();                // 씬을 통째로 갈아 끼운 뒤 (새 씬·불러오기·프로젝트 전환): 이력을 비우고 현재 씬을 첫 상태로
    // 콘텐츠 브라우저 드롭. 모델(Assets 상대 경로)을 바닥면이 position.y 에 닿도록 position 에 놓음.
    void PlaceModel(const std::wstring& relativePath, const DirectX::XMFLOAT3& position);

    // 재생 (에디터의 ▶). Play 는 씬+카메라를 JSON 문자열로 스냅샷한 뒤 Scene::BeginPlay 를 호출하고, Stop 은
    // 스냅샷으로 되돌림.
    enum class PlayState : uint8_t { Editing, Playing, Paused };
    void StartPlay();
    void StopPlay();
    void TogglePause();
    void StepFrame();   // Paused 에서 고정 스텝 한 번

    // 프로젝트
    bool OpenProjectAndScene(const std::wstring& pathOrDir);            // AppBase::OpenProject + 에디터 갱신 + 시작 씬 로드 + 최근 목록
    bool CreateProject(const std::wstring& parentDir, const std::string& name, std::string& error);   // 폴더·스크립트 예제·솔루션·Main.json
    // 스크립트 DLL. BuildScripts = MSBuild <Name>Scripts → ReloadScripts. ReloadScripts = 씬 스냅샷 → 씬 비움
    // → DLL 교체 → 스냅샷 복원. 편집 중에만 다시 올림 (재생 중이면 정지 뒤로 미룸). OnUpdate 가 1초마다 DLL
    // 의 수정 시각을 보고, VS 에서 빌드해도 자동으로 다시 올림.
    void BuildScripts();
    bool ReloadScripts();
    void PollScripts(float dt);
    void SyncProjectInfo();                                              // Editor::project 채우기 + 창 제목
    const wchar_t* GetWindowTitle() const override { return L"SherlockEditor"; }
    const char* GetSceneName() const;

    // WASD/QE 이동 + 우클릭 드래그 회전. ImGui가 입력을 쓰는 동안은 무시함.
    void UpdateCamera(float dt);
    void BeginLook();
    void EndLook();   // 여러 번 불러도 안전함 (포커스 상실·캡처 상실에서도 호출됨)

private:
    SceneMode m_sceneMode = SceneMode::Demo;
    Editor m_editor;
    Editor::Callbacks m_editorCallbacks;
    ModelStats m_modelStats;   // 모델 씬일 때의 로드 통계 (패널 표시)

    // 자동 검증 (실행 인자로 에디터를 조작함). 인자 목록은 Editor/EditorAutomation.h, 구현은 Editor/EditorAutomation.cpp.
    EditorAutomation m_auto;
    bool m_forceBusy = false;   // 자동 검증(--gesture)이 켬: TrackHistory 가 입력 진행 중으로 봄
    void ParseAutomation();
    void RunAutomation();

    // 데모 씬의 움직임은 스크립트(PlayerController·Rotator)와 엔진 컴포넌트(Rigidbody)가 맡고 재생 중에만 동작함.
    EditorHistory m_history;
    std::vector<PendingCommand> m_commands;
    PlayState m_playState = PlayState::Editing;
    std::string m_playSnapshot;   // 재생 전 씬 JSON (SceneSerializer::SaveToString)
    float m_timeScale = 1.0f;
    bool m_stepOnce = false;

    float m_scriptPollTimer = 0.0f;   // PollScripts 간격 (1초)
    size_t m_scriptSources = 0;       // Scripts\**\*.cpp 개수 (PollScripts 가 갱신)

    // F7 로 방향광 0 을 Y축 둘레로 회전시킴.
    bool m_lightOrbit = false;
    float m_lightAngle = 0.0f;

    // 고정 스텝 검증용 카운터 (패널에 표시). OnFixedUpdate 호출 수 / 누적 시간.
    uint32_t m_fixedUpdates = 0;
    float m_fixedTime = 0.0f;

    // 마우스 회전 상태
    bool m_lookActive = false;
    POINT m_lookAnchorScreen = {};   // 드래그 시작 시 커서 위치(화면 좌표). 매 프레임 커서를 여기로 되돌림.
    float m_lookSensitivity = 0.0025f;   // 라디안 / 픽셀
    float m_moveSpeed = 10.0f;           // 단위 / 초. Shift로 4배.
};

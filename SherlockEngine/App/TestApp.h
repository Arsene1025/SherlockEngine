#pragma once
#include "App/AppBase.h"
#include "Graphics/Model.h"   // ModelStats
#include "App/Editor.h"

// 테스트 앱 (10단계부터 "엔진 위의 앱"). 씬 구성·카메라 컨트롤러·단축키·패널만 갖는다.
// Device·Renderer·Scene·Input·Time 은 GetEngine() 으로 쓴다. 렌더 순서는 Engine 의 몫.
class TestApp : public AppBase
{
public:
    // 9단계: 씬 세 가지. F11 로 순환, 설정 engine.scene 또는 실행 인자 --scene=demo|helmet|sponza.
    enum class SceneMode : uint8_t
    {
        Demo,       // 3~6단계의 프리미티브 씬 (애니메이션)
        Helmet,     // Khronos DamagedHelmet (.glb, 텍스처 내장, 탄젠트 없음 → 생성)
        Sponza,     // Khronos Sponza (.gltf + .bin + 외부 이미지, 재질 25개, 알파 컷아웃)
        Count,
        File        // 11단계: JSON 에서 로드한 씬 (F11 순환에는 들어가지 않는다)
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
    void AddPrimitive(int type);     // 0 구, 1 큐브, 2 원기둥, 3 평면
    void DeleteObject(int index);
    const char* GetSceneName() const;

    // WASD/QE 이동 + 우클릭 드래그 회전. ImGui가 입력을 쓰는 동안은 무시한다.
    void UpdateCamera(float dt);
    void BeginLook();
    void EndLook();   // 여러 번 불러도 안전하다 (포커스 상실·캡처 상실에서도 호출됨)

private:
    SceneMode m_sceneMode = SceneMode::Demo;
    Editor m_editor;               // 11단계
    Editor::Callbacks m_editorCallbacks;
    ModelStats m_modelStats;   // 모델 씬일 때의 로드 통계 (패널 표시)

    // 11단계: 자동 검증 (실행 인자). OS 입력 주입 없이 에디터 경로를 지난다 — 화면 캡처 대신 Device 리드백 스크린샷.
    //   --exit-after=N        N 프레임 뒤 종료 (자동 모드 스위치: 창은 화면 밖, 포커스 없음)
    //   --pick=u,v            5 프레임째 씬 뷰의 (u,v) 픽셀(0..1)로 Editor::Pick — 클릭 선택과 같은 코드
    //   --set-position=x,y,z  8 프레임째 선택 오브젝트 위치 변경 (인스펙터 편집과 같은 효과)
    //   --save=path           12 프레임째 씬 저장,   --load=path  16 프레임째 씬 로드
    //   --dump-objects        20 프레임째 오브젝트 이름·위치를 로그로
    //   --screenshot=path     N−1 프레임째 백버퍼 PNG
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
        int addPrimitive = -1;      // --add=0..3 (4 프레임째 오브젝트 추가)
    } m_auto;
    void ParseAutomation();
    void RunAutomation();

    // 애니메이션 대상. Scene의 오브젝트 벡터는 BuildScene 이후 크기가 바뀌지 않으므로 인덱스로 든다.
    size_t m_orbitSphere = 0;
    size_t m_bobCube = 0;
    size_t m_spinCylinder = 0;
    size_t m_pulseSphere = 0;

    // 6단계: F7 로 방향광 0 을 Y축 둘레로 돌린다.
    bool m_lightOrbit = false;
    float m_lightAngle = 0.0f;

    // 7단계: F10 으로 애니메이션을 t = 0 에 고정한다 (픽셀 비교용 결정적 상태).
    bool m_freeze = false;

    // 10단계: 고정 스텝 검증용 카운터 (패널에 표시). OnFixedUpdate 호출 수 / 누적 시간.
    uint32_t m_fixedUpdates = 0;
    float m_fixedTime = 0.0f;

    // 마우스 회전 상태
    bool m_lookActive = false;
    POINT m_lookAnchorScreen = {};   // 드래그 시작 시 커서 위치(화면 좌표). 매 프레임 여기로 되돌린다.
    float m_lookSensitivity = 0.0025f;   // 라디안 / 픽셀
    float m_moveSpeed = 10.0f;           // 단위 / 초. Shift로 4배.
};

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <winerror.h>   // HRESULT의 typedef가 여기에 있음. windows.h를 통째로
                        // 끌어오지 않고도 이 헤더만으로 컴파일되게 하기 위함.

// Logger (10단계, D16 본체).
//
// 0단계의 자유 함수 파사드(Log::Info/Warn/Error)는 그대로 두고 내부 구현을 채웠음. 호출부 100여 곳은 바뀌지 않았음.
//   - 레벨: Debug < Info < Warn < Error. SetMinLevel 로 정한 레벨보다 낮은 로그는 어디에도 출력되지 않음.
//   - 싱크 네 개: 콘솔(stdout), OutputDebugString(VS 출력 창), 파일(exe 기준 경로), 메모리 히스토리(ImGui 콘솔).
//   - 콘솔: SubSystem 이 Windows 라 콘솔이 없음(D19). Init 이 부모 콘솔에 붙거나(AttachConsole), 부모 콘솔이 없고 allocConsole 이 true 면 새로 엶.
//     stdout 이 이미 파이프로 리다이렉트되어 있으면(캡처 스크립트) 그대로 씀.
//   - 스레드 안전: 뮤텍스 하나로 보호함. 로더 스레드가 생겨도 줄이 섞이지 않음.
//
// namespace 로 둔 이유는 0단계 주석과 같음: 상태 없는(정확히는 파일 지역 상태만 있는) 자유 함수 묶음에는
// 클래스보다 namespace 가 적합하고, 싱글턴 인스턴스를 넘겨 다니지 않아도 됨.
namespace Log
{
    enum class Level : uint8_t
    {
        Debug = 0,
        Info,
        Warn,
        Error,
    };

    struct Entry
    {
        Level level;
        double time;          // Init 이후 경과 시간(초)
        std::string text;     // 접두어 없는 본문
    };

    struct InitDesc
    {
        bool console = true;            // 콘솔 싱크. 부모 콘솔에 붙고, 부모 콘솔이 없을 때 allocConsole 이 true 면 새로 엶
        bool allocConsole = false;      // 콘솔이 전혀 없을 때 AllocConsole (Debug 빌드용)
        std::wstring filePath;          // 비면 파일 싱크 없음. 절대 경로
        Level minLevel = Level::Info;
        size_t historyCapacity = 4000;  // ImGui 콘솔용 링 버퍼
    };

    // 한 번만 호출함. 인자 없는 Init 은 0단계에서 콘솔 코드페이지만 맞췄음 — 지금은 InitDesc 기본값으로 부르는 것과 같음.
    void Init();
    void Init(const InitDesc& desc);
    void Shutdown();   // 파일을 닫음. 이후 로그는 콘솔·디버거·히스토리로만 나감

    // 서식은 printf 규칙을 따름.
    // 주의: 컴파일러 메시지처럼 %가 들어갈 수 있는 문자열은 Log::Error("%s", text) 처럼 인자로 넘길 것.
    void Debug(const char* fmt, ...);   // "[디버그] " — 기본 레벨(Info)에서는 출력되지 않음
    void Info(const char* fmt, ...);    // "[정보] "
    void Warn(const char* fmt, ...);    // "[경고] "
    void Error(const char* fmt, ...);   // "[실패] "

    void SetMinLevel(Level level);
    Level GetMinLevel();
    const char* LevelName(Level level);   // "디버그"/"정보"/"경고"/"실패"
    bool ParseLevel(const std::string& text, Level& out);   // "debug|info|warn|error"

    // ImGui 콘솔용 히스토리 스냅샷. 뮤텍스 안에서 복사함. 수천 줄 규모라 프레임마다 불러도 충분히 가벼움.
    void CopyHistory(std::vector<Entry>& out);
    void ClearHistory();
    uint64_t GetHistoryVersion();            // 새 줄이 들어올 때마다 증가 (자동 스크롤 판단용)
    uint32_t GetCount(Level level);          // Init 이후 레벨별 누적 수
    const std::wstring& GetFilePath();       // 열린 파일 경로. 없으면 빈 문자열
    bool HasConsole();

    // HRESULT를 "0x80070002 (지정된 파일을 찾을 수 없습니다)" 형태의 문자열로 바꿈.
    std::string HrToString(HRESULT hr);

    // 넓은 문자열을 UTF-8로 바꿈. std::cout에 wchar_t*를 넘기면
    // 문자열이 아니라 포인터 주소가 찍히기 때문에 필요함.
    std::string ToUtf8(const wchar_t* wide);
}

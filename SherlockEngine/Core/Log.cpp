#include "pch.h"
#include "Core/Log.h"

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <io.h>
#include <fcntl.h>

// 이름 없는 namespace: Log 의 구현 세부. 다른 번역 단위에서 이름으로 참조할 수 없음.
namespace
{
    struct State
    {
        std::mutex mutex;
        Log::Level minLevel = Log::Level::Info;
        bool initialized = false;
        bool console = false;
        std::wstring filePath;
        std::ofstream file;
        std::deque<Log::Entry> history;
        size_t historyCapacity = 4000;
        uint64_t historyVersion = 0;
        uint32_t counts[4] = {};
        LARGE_INTEGER frequency = {};
        LARGE_INTEGER start = {};
    };

    State& S()
    {
        static State state;
        return state;
    }

    // UTF-8 바이트열을 UTF-16으로 바꿔 OutputDebugStringW에 넘김.
    // OutputDebugStringA는 콘솔 코드페이지가 아니라 시스템 ANSI 코드페이지로 해석하므로 한글이 깨짐.
    void WriteToDebugger(const std::string& utf8)
    {
        if (utf8.empty()) return;
        const int need = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        if (need <= 0) return;
        std::wstring wide;
        wide.resize(static_cast<size_t>(need));
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &wide[0], need);
        ::OutputDebugStringW(wide.c_str());
    }

    std::string FormatV(const char* fmt, va_list args)
    {
        va_list copy;
        va_copy(copy, args);
        const int need = std::vsnprintf(nullptr, 0, fmt, copy);
        va_end(copy);
        if (need <= 0) return std::string();
        std::string out;
        out.resize(static_cast<size_t>(need));
        std::vsnprintf(&out[0], static_cast<size_t>(need) + 1, fmt, args);
        return out;
    }

    double Now(State& s)
    {
        if (s.frequency.QuadPart == 0) return 0.0;
        LARGE_INTEGER now;
        ::QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - s.start.QuadPart) / static_cast<double>(s.frequency.QuadPart);
    }

    const char* Prefix(Log::Level level)
    {
        switch (level)
        {
        case Log::Level::Debug: return "[디버그] ";
        case Log::Level::Info:  return "[정보] ";
        case Log::Level::Warn:  return "[경고] ";
        default:                return "[실패] ";
        }
    }

    void Emit(Log::Level level, const char* fmt, va_list args)
    {
        State& s = S();
        if (level < s.minLevel) return;

        const std::string body = FormatV(fmt, args);
        const double time = Now(s);

        // 콘솔·디버거에는 0단계와 같은 형식(접두어 + 본문)으로 씀. 파일에는 시각을 붙임.
        const std::string line = std::string(Prefix(level)) + body + "\n";
        char stamp[32];
        std::snprintf(stamp, sizeof(stamp), "[%9.3f] ", time);

        std::lock_guard<std::mutex> lock(s.mutex);
        ++s.counts[static_cast<int>(level)];
        if (s.console)
        {
            std::fputs(line.c_str(), stdout);
            if (level >= Log::Level::Warn) std::fflush(stdout);
        }
        WriteToDebugger(line);
        if (s.file.is_open())
        {
            s.file << stamp << line;
            s.file.flush();   // 크래시 직전 줄까지 남아야 로그로서 쓸모가 있음
        }
        if (s.historyCapacity > 0)
        {
            if (s.history.size() >= s.historyCapacity) s.history.pop_front();
            s.history.push_back(Log::Entry{ level, time, body });
            ++s.historyVersion;
        }
    }

    // 콘솔 준비. stdout 이 이미 유효하면(리다이렉트, 콘솔 서브시스템) 그대로 씀. 아니면 부모 콘솔에 붙고, 부모 콘솔이 없으면 allocIfNone 일 때만 새로 엶.
    bool SetupConsole(bool allocIfNone)
    {
        const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
        bool attached = out != nullptr && out != INVALID_HANDLE_VALUE;
        if (!attached)
        {
            if (::AttachConsole(ATTACH_PARENT_PROCESS)) attached = true;
            else if (allocIfNone && ::AllocConsole()) attached = true;
            if (!attached) return false;
            FILE* stream = nullptr;
            freopen_s(&stream, "CONOUT$", "w", stdout);
            freopen_s(&stream, "CONOUT$", "w", stderr);
        }
        ::SetConsoleOutputCP(CP_UTF8);
        std::setvbuf(stdout, nullptr, _IOLBF, 4096);
        return true;
    }
}

namespace Log
{
    void Init()
    {
        Init(InitDesc{});
    }

    void Init(const InitDesc& desc)
    {
        State& s = S();
        if (s.initialized) return;
        ::QueryPerformanceFrequency(&s.frequency);
        ::QueryPerformanceCounter(&s.start);
        s.minLevel = desc.minLevel;
        s.historyCapacity = desc.historyCapacity;
        s.console = desc.console && SetupConsole(desc.allocConsole);
        if (!desc.filePath.empty())
        {
            // 폴더가 없으면 만듦 (한 단계만).
            const size_t slash = desc.filePath.find_last_of(L"\\/");
            if (slash != std::wstring::npos) ::CreateDirectoryW(desc.filePath.substr(0, slash).c_str(), nullptr);
            s.file.open(desc.filePath, std::ios::out | std::ios::trunc | std::ios::binary);
            if (s.file.is_open())
            {
                s.filePath = desc.filePath;
                s.file << "\xEF\xBB\xBF";   // UTF-8 BOM: 메모장이 한글을 바로 읽도록
            }
        }
        s.initialized = true;
        if (!s.filePath.empty()) Info("로그 파일: %s", ToUtf8(s.filePath.c_str()).c_str());
        else if (!desc.filePath.empty()) Warn("로그 파일을 열 수 없음: %s", ToUtf8(desc.filePath.c_str()).c_str());
    }

    void Shutdown()
    {
        State& s = S();
        std::lock_guard<std::mutex> lock(s.mutex);
        if (s.file.is_open()) s.file.close();
        s.filePath.clear();
    }

    void Debug(const char* fmt, ...) { va_list args; va_start(args, fmt); Emit(Level::Debug, fmt, args); va_end(args); }
    void Info(const char* fmt, ...)  { va_list args; va_start(args, fmt); Emit(Level::Info, fmt, args);  va_end(args); }
    void Warn(const char* fmt, ...)  { va_list args; va_start(args, fmt); Emit(Level::Warn, fmt, args);  va_end(args); }
    void Error(const char* fmt, ...) { va_list args; va_start(args, fmt); Emit(Level::Error, fmt, args); va_end(args); }

    void SetMinLevel(Level level) { S().minLevel = level; }
    Level GetMinLevel() { return S().minLevel; }

    const char* LevelName(Level level)
    {
        switch (level)
        {
        case Level::Debug: return "디버그";
        case Level::Info:  return "정보";
        case Level::Warn:  return "경고";
        default:           return "실패";
        }
    }

    bool ParseLevel(const std::string& text, Level& out)
    {
        std::string lower;
        for (char c : text) lower += static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        if (lower == "debug") { out = Level::Debug; return true; }
        if (lower == "info")  { out = Level::Info;  return true; }
        if (lower == "warn" || lower == "warning") { out = Level::Warn; return true; }
        if (lower == "error") { out = Level::Error; return true; }
        return false;
    }

    void CopyHistory(std::vector<Entry>& out)
    {
        State& s = S();
        std::lock_guard<std::mutex> lock(s.mutex);
        out.assign(s.history.begin(), s.history.end());
    }

    void ClearHistory()
    {
        State& s = S();
        std::lock_guard<std::mutex> lock(s.mutex);
        s.history.clear();
        ++s.historyVersion;
    }

    uint64_t GetHistoryVersion() { return S().historyVersion; }
    uint32_t GetCount(Level level) { return S().counts[static_cast<int>(level)]; }
    const std::wstring& GetFilePath() { return S().filePath; }
    bool HasConsole() { return S().console; }

    std::string HrToString(HRESULT hr)
    {
        char code[16] = {};
        std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned int>(hr));

        LPWSTR text = nullptr;
        const DWORD len = ::FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, static_cast<DWORD>(hr), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&text), 0, nullptr);

        std::string result = code;
        if (len > 0 && text != nullptr)
        {
            std::string message = ToUtf8(text);
            // FormatMessage는 끝에 개행을 붙여 줌. 한 줄 로그이므로 떼어 냄.
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) message.pop_back();
            if (!message.empty()) result += " (" + message + ")";
        }
        if (text != nullptr) ::LocalFree(text);
        return result;
    }

    std::string ToUtf8(const wchar_t* wide)
    {
        if (wide == nullptr) return std::string();
        const int need = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (need <= 1) return std::string();
        std::string result;
        result.resize(static_cast<size_t>(need) - 1);
        ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], need, nullptr, nullptr);
        return result;
    }
}

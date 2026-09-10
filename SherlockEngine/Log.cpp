#include "pch.h"
#include "Log.h"

#include <cstdarg>
#include <cstdio>

// 이름 없는 namespace(anonymous namespace).
//
// Log.h의 namespace Log가 이름을 묶어 구분하는 쪽이라면, 이쪽은 정반대로
// 이름을 이 파일 안에 가두는 쪽이다. 여기 들어간 것은 내부 링키지를 얻어
// 다른 번역 단위에서 이름으로 참조할 수 없게 되므로, 다른 .cpp에 우연히
// 같은 이름의 헬퍼가 있어도 충돌하지 않는다. C의 파일 지역 static과 같은
// 역할이고, C++에서는 이쪽이 권장되는 방식이다.
//
// 아래 셋은 Log의 구현 세부일 뿐 바깥에 보일 이유가 없으므로 여기에 둔다.
namespace
{
    // UTF-8 바이트열을 UTF-16으로 바꿔 OutputDebugStringW에 넘긴다.
    // OutputDebugStringA는 콘솔 코드페이지가 아니라 시스템 ANSI 코드페이지로
    // 해석하므로 한글이 깨진다. W 버전을 써야 한다.
    void WriteToDebugger(const std::string& utf8)
    {
        if (utf8.empty())
        {
            return;
        }

        const int need = ::MultiByteToWideChar(
            CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
        if (need <= 0)
        {
            return;
        }

        std::wstring wide;
        wide.resize(static_cast<size_t>(need));
        ::MultiByteToWideChar(
            CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &wide[0], need);

        ::OutputDebugStringW(wide.c_str());
    }

    std::string FormatV(const char* fmt, va_list args)
    {
        va_list copy;
        va_copy(copy, args);
        const int need = std::vsnprintf(nullptr, 0, fmt, copy);
        va_end(copy);

        if (need <= 0)
        {
            return std::string();
        }

        std::string out;
        out.resize(static_cast<size_t>(need));
        std::vsnprintf(&out[0], static_cast<size_t>(need) + 1, fmt, args);
        return out;
    }

    void Emit(const char* prefix, const char* fmt, va_list args)
    {
        const std::string body = FormatV(fmt, args);
        const std::string line = std::string(prefix) + body + "\n";

        std::cout << line;
        WriteToDebugger(line);
    }
}

// 같은 namespace는 여러 파일에서 몇 번이든 다시 열 수 있다. Log.h가 선언을,
// 여기가 정의를 담당하는 이 분업이 가능한 이유다. 클래스의 정적 멤버 함수로
// 묶었다면 선언 전체가 한 곳에 고정되고 상속·인스턴스화 같은 필요 없는
// 의미까지 딸려온다. 상태 없는 자유 함수 묶음에는 namespace가 맞다.
namespace Log
{
    void Init()
    {
        ::SetConsoleOutputCP(CP_UTF8);
    }

    void Info(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        Emit("[정보] ", fmt, args);
        va_end(args);
    }

    void Warn(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        Emit("[경고] ", fmt, args);
        va_end(args);
    }

    void Error(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        Emit("[실패] ", fmt, args);
        va_end(args);
    }

    std::string HrToString(HRESULT hr)
    {
        char code[16] = {};
        std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned int>(hr));

        LPWSTR text = nullptr;
        const DWORD len = ::FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            static_cast<DWORD>(hr),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&text),
            0,
            nullptr);

        std::string result = code;
        if (len > 0 && text != nullptr)
        {
            std::string message = ToUtf8(text);
            // FormatMessage는 끝에 개행을 붙여 준다. 한 줄 로그이므로 떼어낸다.
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
            {
                message.pop_back();
            }
            if (!message.empty())
            {
                result += " (" + message + ")";
            }
        }

        if (text != nullptr)
        {
            ::LocalFree(text);
        }

        return result;
    }

    std::string ToUtf8(const wchar_t* wide)
    {
        if (wide == nullptr)
        {
            return std::string();
        }

        const int need = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (need <= 1)
        {
            return std::string();
        }

        std::string result;
        result.resize(static_cast<size_t>(need) - 1);
        ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], need, nullptr, nullptr);
        return result;
    }
}

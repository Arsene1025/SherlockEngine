#include "pch.h"
#include "Log.h"

#include <cstdarg>
#include <cstdio>

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

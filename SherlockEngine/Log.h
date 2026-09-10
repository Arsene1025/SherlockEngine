#pragma once
#include <string>

// 최소 로그 파사드.
// 콘솔과 OutputDebugString 양쪽으로 보낸다. VS 출력 창에서 D3D Debug Layer
// 메시지와 같은 곳에 나오게 하는 것이 목적이다.
//
// 레벨 필터, 파일 싱크, ImGui 콘솔은 여기에 없다. 10단계 Logger의 몫이다.
// 지금 필요한 것은 "std::cout 직접 호출을 한 곳으로 모으는 것"뿐이다.
namespace Log
{
    // 콘솔 출력 코드페이지를 UTF-8로 맞춘다. main()에서 딱 한 번 부를 것.
    // 소스가 /utf-8로 컴파일되므로 문자열 리터럴은 UTF-8 바이트다.
    // 이 호출이 없으면 한글 메시지가 CP949 콘솔에서 깨진다.
    void Init();

    // 서식은 printf 규칙을 따른다.
    // 주의: 컴파일러 메시지처럼 %가 들어갈 수 있는 문자열은
    //       Log::Error("%s", text) 처럼 인자로 넘길 것.
    void Info(const char* fmt, ...);    // "[정보] "
    void Warn(const char* fmt, ...);    // "[경고] "
    void Error(const char* fmt, ...);   // "[실패] "

    // HRESULT를 "0x80070002 (지정된 파일을 찾을 수 없습니다)" 형태로.
    std::string HrToString(HRESULT hr);

    // 넓은 문자열을 UTF-8로. std::cout에 wchar_t*를 넘기면
    // 문자열이 아니라 포인터 주소가 찍히기 때문에 필요하다.
    std::string ToUtf8(const wchar_t* wide);
}

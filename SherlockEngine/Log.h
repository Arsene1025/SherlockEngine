#pragma once
#include <string>
#include <winerror.h>   // HRESULT의 typedef가 여기에 있다. windows.h를 통째로
                        // 끌어오지 않고 이 헤더 혼자서도 컴파일되게 하려는 것.
                        // 헤더는 자기가 쓰는 타입을 스스로 책임져야 한다.

// 최소 로그 파사드.
// 콘솔과 OutputDebugString 양쪽으로 보낸다. VS 출력 창에서 D3D Debug Layer
// 메시지와 같은 곳에 나오게 하는 것이 목적이다.
//
// 레벨 필터, 파일 싱크, ImGui 콘솔은 여기에 없다. 10단계 Logger의 몫이다.
// 지금 필요한 것은 "std::cout 직접 호출을 한 곳으로 모으는 것"뿐이다.

// namespace에 대하여.
//
// namespace는 "전역 함수를 만드는 장치"가 아니라 이미 전역인 이름을 묶어
// 충돌을 막는 장치다. 아래 함수들은 namespace에 넣든 말든 전역 함수이며,
// Log:: 라는 한정자가 붙어서 다른 코드의 Info/Warn/Error와 구별될 뿐이다.
// 저장 기간(storage duration)과 링키지(linkage)는 namespace가 건드리지 않는다.
// namespace가 없었다면 Error 같은 흔한 이름이 전역에 그대로 노출되어,
// 같은 이름을 쓰는 라이브러리가 하나만 끼어들어도 링크가 깨진다.
//
// 따라서 namespace는 가시성을 넓히는 도구가 아니라 좁히고 구분하는 도구다.
// 이 헤더를 include하지 않은 .cpp에서는 Log:: 를 쓸 수 없다. 컴파일러는
// 번역 단위(.cpp) 하나하나를 독립적으로 컴파일하므로, 이름이 namespace 안에
// 있다는 사실이 선언을 대신해 주지 못한다. 쓰는 파일마다 #include "Log.h"가
// 필요하다. pch.h에는 "거의 모든 파일이 쓰는 것"만 두는 규칙이라 Log.h는
// 거기에 없고, 그래서 자동으로 딸려오지 않는다.
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

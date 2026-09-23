#pragma once
#include <string>

// 11-C단계: 에디터에서 새 스크립트 만들기 (유니티의 Create > C# Script, 언리얼의 New C++ Class).
//
// 11-E단계: 스크립트는 프로젝트의 것이다. <프로젝트>\Scripts\<이름>.h (선언) 와 <이름>.cpp (구현 + SHERLOCK_SCRIPT) 를 템플릿으로 만든다.
// 프로젝트 솔루션(ProjectGenerator)과 엔진 솔루션의 SherlockEditor/SherlockGame 은 Scripts\*.cpp 를 와일드카드로 컴파일하므로
// vcxproj 에 등록할 것이 없다 — Visual Studio 는 "프로젝트 다시 로드" 후 새 파일을 보인다.
// C++ 이므로 빌드(Ctrl+Shift+B)하고 다시 실행해야 Inspector 목록에 나타난다 — 에디터는 컴파일하지 않는다.
namespace ScriptCreator
{
	bool IsAvailable();                                             // 프로젝트가 열려 있나
	std::wstring GetScriptsDir();                                   // <프로젝트>\Scripts\ (끝 백슬래시)
	std::wstring GetScriptPath(const std::string& typeName);        // 그 스크립트의 .cpp (존재하지 않을 수도 있다)
	std::wstring GetScriptHeaderPath(const std::string& typeName);  // 그 스크립트의 .h
	bool IsValidClassName(const std::string& name);                 // C++ 식별자 + 예약어 아님
	// 파일 두 개 생성. 실패하면 false 와 이유. 이미 있으면 실패.
	bool Create(const std::string& className, std::string& error);
	bool Open(const std::wstring& path);                            // ShellExecute "open" — 연결 프로그램(보통 VS)
	bool OpenScript(const std::string& typeName);                   // .h 와 .cpp 를 연다 (있는 것만)
}

#pragma once
#include <string>

// 11-C단계: 에디터에서 새 스크립트 만들기 (유니티의 Create > C# Script, 언리얼의 New C++ Class).
//
// 11-E단계: 스크립트는 프로젝트에 속함. <프로젝트>\Scripts\<이름>.h (선언) 와 <이름>.cpp (구현 + SHERLOCK_SCRIPT) 를 템플릿으로 생성함.
// 프로젝트 솔루션(ProjectGenerator)과 엔진 솔루션의 SherlockEditor/SherlockGame 은 Scripts\*.cpp 를 와일드카드로 컴파일하므로
// vcxproj 에 따로 등록할 필요가 없음 — Visual Studio 에서는 "프로젝트 다시 로드" 후 새 파일이 보임.
// C++ 이므로 빌드(Ctrl+Shift+B)하고 다시 실행해야 Inspector 목록에 나타남 — 에디터는 컴파일하지 않음.
namespace ScriptCreator
{
	bool IsAvailable();                                             // 프로젝트가 열려 있는지 여부
	std::wstring GetScriptsDir();                                   // <프로젝트>\Scripts\ (끝 백슬래시)
	std::wstring GetScriptPath(const std::string& typeName);        // 그 스크립트의 .cpp (존재하지 않을 수도 있음)
	std::wstring GetScriptHeaderPath(const std::string& typeName);  // 그 스크립트의 .h
	bool IsValidClassName(const std::string& name);                 // C++ 식별자 + 예약어 아님
	// 파일 두 개를 생성함. 실패하면 false 를 반환하고 error 에 이유를 담음. 이미 있으면 실패.
	bool Create(const std::string& className, std::string& error);
	bool Open(const std::wstring& path);                            // ShellExecute "open" — 연결 프로그램(보통 VS)
	bool OpenScript(const std::string& typeName);                   // .h 와 .cpp 를 엶 (있는 파일만)
}

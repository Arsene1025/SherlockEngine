#pragma once
#include <string>
#include <vector>

class Project;

// 11-E단계: 프로젝트 솔루션 생성 — 언리얼의 "Generate Visual Studio project files".
//
// 2026-10-08 (B안): <프로젝트>\<Name>.sln 과 <Name>Scripts.vcxproj 하나를 씀.
//   <Name>Scripts.vcxproj  스크립트 DLL = Scripts\**\*.cpp → <프로젝트>\Binaries\<구성>\<Name>Scripts.dll
// 엔진 DLL 의 import lib(<repo>\x64\<구성>\SherlockEngine.lib)를 링크함. 솔루션에는 엔진·에디터 프로젝트도 넣어
// F5 로 SherlockEditor.exe --project=<이 프로젝트> 를 띄우고 엔진 코드까지 디버깅할 수 있게 함.
// 에디터의 Build scripts 는 /p:BuildProjectReferences=false 로 스크립트 DLL 만 빌드함 (실행 중인 엔진 DLL 은 잠겨 있으므로).
// 스크립트는 와일드카드 항목이므로 New Script 후에 vcxproj 를 고칠 필요가 없음. 중간 파일은 Intermediate\.
// 엔진 저장소 위치는 프로젝트 파일의 engineRoot(…\SherlockEngine\)에서 가져옴. 저장소 위치가 바뀌면 다시 생성해야 함.
namespace ProjectGenerator
{
	bool Generate(const Project& project, std::string& error);

	// vswhere 로 MSBuild 를 찾아 solution 의 target 을 configuration|x64 로 빌드함. 동기 실행이며 출력은 logFile 에 기록함.
	bool RunMsBuild(const std::wstring& solution, const std::wstring& target, const wchar_t* configuration, const std::wstring& logFile, std::string& error, const std::wstring& extraArguments = L"");

	// 엔진 저장소 루트 (<repo>\, SherlockEngine\ 의 부모). 소스 트리가 없으면 빈 문자열.
	std::wstring GetEngineRepoDir();
	// 2026-10-08 (B안): 옛 형식(스크립트를 exe 에 컴파일)이거나 스크립트 DLL 프로젝트가 없으면 다시 생성함. 생성했으면 true.
	bool EnsureUpToDate(const Project& project);
}

#pragma once
#include <string>
#include <vector>

class Project;

// 11-E단계: 프로젝트 솔루션 생성 — 언리얼의 "Generate Visual Studio project files".
//
// <프로젝트>\<Name>.sln 과 vcxproj 두 개를 씀:
//   <Name>Editor.vcxproj  에디터 exe  = 엔진 저장소의 에디터 소스(App\TestApp·Editor·…) + EditorMain.cpp + Scripts\*.cpp
//   <Name>.vcxproj        게임 exe    = GameMain.cpp + Scripts\*.cpp
// 둘 다 엔진 정적 라이브러리(SherlockEngine.vcxproj)를 ProjectReference 로 참조하고 /WHOLEARCHIVE 로 링크함 (11-D).
// 스크립트는 와일드카드 항목이므로 New Script 후에 vcxproj 를 고칠 필요가 없음. SHERLOCK_PROJECT_NAME="<Name>" 을 정의해
// 에디터가 "이 exe 에 이 프로젝트의 스크립트가 들어 있는지" 를 알 수 있게 함. 산출물은 <프로젝트>\Binaries\<구성>\, 중간 파일은 Intermediate\.
// 엔진 저장소 위치는 프로젝트 파일의 engineRoot(…\SherlockEngine\)에서 가져옴. 저장소 위치가 바뀌면 다시 생성해야 함.
namespace ProjectGenerator
{
	bool Generate(const Project& project, std::string& error);

	// vswhere 로 MSBuild 를 찾아 solution 의 target 을 configuration|x64 로 빌드함. 동기 실행이며 출력은 logFile 에 기록함.
	bool RunMsBuild(const std::wstring& solution, const std::wstring& target, const wchar_t* configuration, const std::wstring& logFile, std::string& error);

	// 엔진 저장소 루트 (<repo>\, SherlockEngine\ 의 부모). 소스 트리가 없으면 빈 문자열.
	std::wstring GetEngineRepoDir();
	// 에디터 프로젝트가 컴파일하는 에디터 소스 (App\ 기준 이름). ProjectGenerator 와 SherlockEditor.vcxproj 의 목록이 같아야 함.
	const std::vector<std::wstring>& GetEditorSources();
}

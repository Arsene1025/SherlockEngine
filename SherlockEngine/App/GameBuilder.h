#pragma once
#include <string>
#include <vector>

// 11-D단계: 게임 빌드(패키징) — 언리얼의 Package Project, 유니티의 Build.
//
// Build\<이름>\ 폴더에 SherlockGame.exe + DLL + Shaders\ + Assets\ 를 모으고 engine.ini 에 [game] startScene 을 적는다.
// 폴더를 통째로 복사해 다른 PC 에서 exe 를 더블클릭하면 게임이 뜬다 (소스 트리가 없으므로 Paths 는 exe 옆 사본만 본다).
// releaseBuild 면 먼저 vswhere → MSBuild 로 SherlockGame 을 Release|x64 로 빌드한다 (VS 가 설치된 개발 PC 에서만).
// 에셋은 전부 복사하거나(copyAllAssets), 시작 씬이 참조하는 모델 폴더·텍스처만 복사한다 (Sponza 같은 큰 모델을 빼기 위해).
namespace GameBuilder
{
	struct Options
	{
		std::string name = "MyGame";                  // Build\<name>\ 와 창 제목·exe 이름
		std::string startScene = "";                  // 프로젝트 Assets\Scenes 기준 파일명 (ListScenes 의 항목)
		std::string projectName;                      // 11-E: 열린 프로젝트 이름 — 런타임 exe(<이름>.exe) 와 MSBuild 타깃
		std::wstring projectSolution;                 // 11-E: 프로젝트 솔루션 경로 (없으면 엔진 솔루션의 SherlockGame)
		bool copyAllAssets = true;
		bool releaseBuild = false;                    // MSBuild Release 후 Release 폴더의 런타임을 쓴다. 아니면 지금 에디터 옆(Debug)의 것
		bool openFolder = true;
	};
	struct Result
	{
		bool ok = false;
		std::wstring outputDir;
		std::string message;                          // 마지막 상태/오류 (메뉴 바 메시지)
		int filesCopied = 0;
		double seconds = 0.0;
	};

	bool IsAvailable();                               // 프로젝트가 열려 있나
	std::wstring GetBuildRoot();                      // <프로젝트>\Build 폴더 (끝 백슬래시)
	std::vector<std::string> ListScenes();            // 프로젝트 Assets\Scenes\*.json
	Result Build(const Options& options);             // 동기. Release 빌드를 켜면 몇십 초 걸린다
}

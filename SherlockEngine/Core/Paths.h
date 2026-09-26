#pragma once
#include <string>

// 실행 파일 기준 경로.
//
// 셰이더 같은 런타임 에셋은 "현재 작업 디렉터리"가 아니라 "실행 파일이 있는
// 폴더"를 기준으로 찾아야 함. 작업 디렉터리는 실행 방법(F5, 탐색기 더블클릭,
// 명령줄)에 따라 달라지지만 exe 위치는 항상 같음.
//
// 11-E단계: 루트는 세 가지임.
//   엔진 루트   SherlockEngine\ 소스 폴더 (셰이더 원본, 엔진 콘텐츠 Assets\). 찾는 순서: SetEngineRoot(프로젝트 파일의 engineRoot)
//              → exe\..\..\SherlockEngine\ 마커(엔진 저장소 빌드) → exe 폴더(배포: 전부 exe 옆에 있음)
//   프로젝트 루트  <프로젝트>\ (AppBase 가 .sherlock 을 찾아 SetProjectRoot). 없으면 프로젝트 콘텐츠 = 엔진 콘텐츠
//   exe 폴더    빌드 산출물 (.cso, DLL, 배포 시 Assets\ 사본)
// 에셋 해석은 "프로젝트 Assets → 엔진 Assets → exe\Assets" 순이라 씬 파일에는 어느 루트 기준인지 적지 않음.
namespace Paths
{
	// GetModuleFileNameW로 얻은 exe 폴더. 끝에 백슬래시가 붙어 있음.
	// 첫 호출에서 계산해 캐시함.
	const std::wstring& GetExecutableDir();

	// ---- 11-E단계: 루트 ----
	void SetEngineRoot(const std::wstring& dir);      // 프로젝트 파일이 준 엔진 소스 폴더. 존재할 때만 적용
	const std::wstring& GetEngineRoot();              // 끝에 백슬래시 포함. 배포 환경이면 exe 폴더
	bool HasEngineSourceTree();                       // 엔진 루트가 소스 트리인지 여부 (배포면 false)
	void SetProjectRoot(const std::wstring& dir);     // 빈 문자열 = 프로젝트 없음
	const std::wstring& GetProjectRoot();
	bool HasProject();
	std::wstring GetEngineAssetRoot();                // <엔진 루트>\Assets\ (없으면 exe\Assets\)
	std::wstring GetAssetRoot();                      // 프로젝트 Assets\ (프로젝트가 없으면 엔진 Assets\). 콘텐츠 브라우저의 첫 트리·씬 저장 위치

	// exe\Shaders\<fileName> 의 절대 경로.
	// 빌드가 Shaders\ 폴더를 exe 옆으로 복사함(vcxproj의 CopyShaders 타깃).
	// 그 파일이 없으면 엔진 루트의 Shaders\ 로 폴백하고 경고를 한 번 남김.
	std::wstring GetShaderPath(const wchar_t* fileName);

	// exe\Shaders\<fileName> — 빌드(fxc)가 만든 .cso. 폴백 없음.
	std::wstring GetShaderBinaryPath(const wchar_t* fileName);

	// 엔진 루트의 Shaders\<fileName>. Debug 핫리로드가 감시하는 원본임.
	// 소스 트리가 없으면(배포 환경) exe 옆 사본을 돌려줌.
	std::wstring GetShaderSourcePath(const wchar_t* fileName);

	// Assets\<relative> (예: L"Textures\\uv_checker.png"): 프로젝트 Assets → 엔진 Assets → exe\Assets 순으로 찾아 처음 있는 경로.
	// 아무 데도 없으면 프로젝트(또는 엔진) 경로를 돌려주어 "파일 없음"으로 실패하게 둠.
	std::wstring GetAssetPath(const wchar_t* relative);

	// 씬 파일 폴더 = 프로젝트 Assets\Scenes\ (없으면 엔진). 끝에 백슬래시 포함. 폴더가 없으면 만듦.
	std::wstring GetSceneDir();

	// 절대 경로 → 프로젝트 또는 엔진 Assets 기준 상대 경로(백슬래시). 어느 루트에도 없으면 빈 문자열. 대소문자 무시.
	std::wstring ToAssetRelative(const std::wstring& absolute);
}

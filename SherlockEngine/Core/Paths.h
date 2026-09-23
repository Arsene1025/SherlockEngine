#pragma once
#include <string>

// 실행 파일 기준 경로.
//
// 셰이더 같은 런타임 에셋은 "현재 작업 디렉터리"가 아니라 "실행 파일이 있는
// 폴더"를 기준으로 찾아야 한다. 작업 디렉터리는 실행 방법(F5, 탐색기 더블클릭,
// 명령줄)에 따라 달라지지만 exe 위치는 항상 같다.
namespace Paths
{
	// GetModuleFileNameW로 얻은 exe 폴더. 끝에 백슬래시가 붙어 있다.
	// 첫 호출에서 계산해 캐시한다.
	const std::wstring& GetExecutableDir();

	// exe\Shaders\<fileName> 의 절대 경로.
	// 빌드가 Shaders\ 폴더를 exe 옆으로 복사한다(vcxproj의 CopyShaders 타깃).
	// 그 파일이 없으면 소스 트리(exe\..\..\SherlockEngine\Shaders\)로 폴백하고
	// 한 번 경고를 남긴다. 둘 다 없으면 첫 번째 경로를 그대로 돌려주어
	// D3DCompileFromFile이 "파일 없음"으로 실패하게 둔다.
	std::wstring GetShaderPath(const wchar_t* fileName);

	// exe\Shaders\<fileName> — 빌드(fxc)가 만든 .cso. 폴백 없음.
	std::wstring GetShaderBinaryPath(const wchar_t* fileName);

	// 소스 트리의 Shaders\<fileName>. Debug 핫리로드가 감시하는 원본이다.
	// 소스 트리가 없으면(배포 환경) exe 옆 사본을 돌려준다.
	std::wstring GetShaderSourcePath(const wchar_t* fileName);

	// exe\Assets\<relative> (예: L"Textures\\uv_checker.png"). 빌드가 Assets\ 를 exe 옆으로 복사한다.
	// 없으면 소스 트리의 Assets\ 로 폴백.
	std::wstring GetAssetPath(const wchar_t* relative);
}

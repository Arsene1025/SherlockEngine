#pragma once
#include <string>

// 11-E단계: 프로젝트 = 폴더 하나 = 게임 하나 (언리얼의 .uproject).
//
//   <Name>\
//   ├─ <Name>.sherlock     JSON: name, startScene, engineRoot (엔진 소스 폴더 — 프로젝트 솔루션, 셰이더, 엔진 콘텐츠를 찾는 기준)
//   ├─ Assets\             프로젝트 콘텐츠 (Scenes\, Textures\, Models\ …). 엔진 콘텐츠(SherlockEngine\Assets)는 후순위 폴백으로 보임
//   ├─ Scripts\            프로젝트 스크립트 (.h/.cpp). <Name>Scripts.vcxproj 가 와일드카드(Scripts\**\*.cpp)로 컴파일함
//   ├─ <Name>.sln / <Name>Scripts.vcxproj   에디터가 생성 (ProjectGenerator). 엔진 DLL 의 import lib 를 링크
//   ├─ Binaries\<구성>\    <Name>Scripts.dll (2026-10-08 B안: 에디터·게임이 LoadLibrary 로 올림)
//   └─ Build\              패키징 결과
//
// 엔진 컴포넌트(Rigidbody·FollowTarget·CameraComponent)는 엔진 DLL 에 있어 어떤 프로젝트에서도 쓸 수 있음. 프로젝트 스크립트만 프로젝트에 속함.
// 에디터(SherlockEditor.exe)와 게임(SherlockGame.exe)은 모든 프로젝트가 공유하는 하나뿐임 — 프로젝트별 exe 는 없음 (언리얼의 게임 모듈 DLL 방식).
class Project
{
public:
	static constexpr const wchar_t* kExtension = L".sherlock";

	bool Load(const std::wstring& pathOrDir);                  // .sherlock 파일 또는 그것이 든 폴더
	bool Save() const;
	bool IsLoaded() const { return !m_root.empty(); }

	// 폴더 골격(Assets\Scenes, Assets\Models, Assets\Textures, Scripts, Build)과 .sherlock 을 만듦. 이미 있으면 실패.
	static bool Create(const std::wstring& parentDir, const std::string& name, const std::wstring& engineRoot, Project& out, std::string& error);
	static std::wstring FindProjectFile(const std::wstring& dir);              // dir 안의 첫 *.sherlock. 없으면 빈 문자열
	static std::wstring FindUpwards(const std::wstring& startDir, int maxLevels);   // startDir 부터 위로 올라가며 찾음 (Binaries\Debug → 프로젝트)
	static bool IsValidName(const std::string& name);

	const std::string& GetName() const { return m_name; }
	const std::wstring& GetRoot() const { return m_root; }         // 끝에 백슬래시 포함
	std::wstring GetFilePath() const;                              // <root><Name>.sherlock
	std::wstring GetAssetsDir() const { return m_root + L"Assets\\"; }
	std::wstring GetScriptsDir() const { return m_root + L"Scripts\\"; }
	std::wstring GetBinariesDir(const wchar_t* configuration) const { return m_root + L"Binaries\\" + configuration + L"\\"; }
	std::wstring GetSolutionPath() const;
	std::wstring GetScriptsProjectName() const;                    // "<Name>Scripts" (vcxproj 이름이자 DLL 이름)

	std::string startScene;      // Assets\Scenes 기준 파일명
	std::wstring engineRoot;     // SherlockEngine\ 소스 폴더 (생성 시 기록). 값이 비었거나 그 폴더가 없으면 exe 옆에서 찾음

private:
	std::string m_name;
	std::wstring m_root;
};

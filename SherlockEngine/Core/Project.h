#pragma once
#include <string>

// 11-E단계: 프로젝트 = 폴더 하나 = 게임 하나 (언리얼의 .uproject).
//
//   <Name>\
//   ├─ <Name>.sherlock     JSON: name, startScene, engineRoot (엔진 소스 폴더 — 프로젝트 솔루션과 셰이더·엔진 콘텐츠를 찾는다)
//   ├─ Assets\             프로젝트 콘텐츠 (Scenes\, Textures\, Models\ …). 엔진 콘텐츠(SherlockEngine\Assets)는 뒤에서 폴백으로 보인다
//   ├─ Scripts\            프로젝트 스크립트 (.h/.cpp). 프로젝트 솔루션이 와일드카드(Scripts\*.cpp)로 컴파일한다
//   ├─ <Name>.sln / <Name>Editor.vcxproj / <Name>.vcxproj   에디터가 생성 (ProjectGenerator). 엔진 lib 를 참조
//   ├─ Binaries\<구성>\    <Name>Editor.exe, <Name>.exe
//   └─ Build\              패키징 결과
//
// 엔진 컴포넌트(Rigidbody·FollowTarget·CameraComponent)는 엔진 lib 에 있어 어떤 프로젝트에서도 쓰인다. 프로젝트 스크립트만 프로젝트에 속한다.
// 엔진 저장소의 Projects\Sample 이 예제 프로젝트이고, 엔진 솔루션의 SherlockEditor/SherlockGame 이 그 프로젝트의 에디터·게임이다.
class Project
{
public:
	static constexpr const wchar_t* kExtension = L".sherlock";

	bool Load(const std::wstring& pathOrDir);                  // .sherlock 파일 또는 그것이 든 폴더
	bool Save() const;
	bool IsLoaded() const { return !m_root.empty(); }

	// 폴더 골격(Assets\Scenes, Assets\Textures, Scripts, Build)과 .sherlock 을 만든다. 이미 있으면 실패.
	static bool Create(const std::wstring& parentDir, const std::string& name, const std::wstring& engineRoot, Project& out, std::string& error);
	static std::wstring FindProjectFile(const std::wstring& dir);              // dir 안의 첫 *.sherlock. 없으면 빈 문자열
	static std::wstring FindUpwards(const std::wstring& startDir, int maxLevels);   // startDir 부터 위로 올라가며 찾는다 (Binaries\Debug → 프로젝트)
	static bool IsValidName(const std::string& name);

	const std::string& GetName() const { return m_name; }
	const std::wstring& GetRoot() const { return m_root; }         // 끝 백슬래시
	std::wstring GetFilePath() const;                              // <root><Name>.sherlock
	std::wstring GetAssetsDir() const { return m_root + L"Assets\\"; }
	std::wstring GetScriptsDir() const { return m_root + L"Scripts\\"; }
	std::wstring GetBinariesDir(const wchar_t* configuration) const { return m_root + L"Binaries\\" + configuration + L"\\"; }
	std::wstring GetSolutionPath() const;
	std::wstring GetEditorProjectName() const;                     // "<Name>Editor"

	std::string startScene;      // Assets\Scenes 기준 파일명
	std::wstring engineRoot;     // SherlockEngine\ 소스 폴더 (생성 시 기록). 없거나 존재하지 않으면 exe 옆에서 찾는다

private:
	std::string m_name;
	std::wstring m_root;
};

#include "pch.h"
#include "App/ProjectGenerator.h"
#include "Core/Project.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
	const wchar_t* kEngineGuid = L"{DCC06A61-7D5A-44F5-A959-3D9367E30EE7}";   // SherlockEngine.vcxproj 의 ProjectGuid
	const wchar_t* kEditorGuid = L"{6F2E9A10-3C4D-4B5E-9F60-1E2D3C4B5A69}";   // SherlockEditor.vcxproj 의 ProjectGuid (엔진 저장소의 것을 그대로 참조)
	const wchar_t* kScriptsGuid = L"{8B4C1D32-5E6F-4D70-B182-3F4E5D6C7B8C}";  // 생성 프로젝트의 스크립트 DLL (모든 프로젝트가 같은 GUID — 솔루션마다 하나뿐)

	// 2026-10-08 (B안): 생성기의 형식 번호. vcxproj 첫 주석에 들어가며, EnsureUpToDate 가 이 표시가 없는 프로젝트(옛 형식)를 다시 생성함.
	// 1 = 11-E 의 <Name>Editor.vcxproj + <Name>.vcxproj (스크립트를 exe 에 컴파일), 2 = <Name>Scripts.vcxproj (스크립트 DLL).
	const wchar_t* kGeneratorMarker = L"SherlockEngine ProjectGenerator 2";
	const char* kGeneratorMarkerUtf8 = "SherlockEngine ProjectGenerator 2";
	const char* kOldGeneratorMarkerUtf8 = "ProjectGenerator 가 만들었다";   // 형식 1 의 주석. 이 표시가 있는 옛 파일만 지움

	std::wstring Wide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
		return wide;
	}

	bool WriteAll(const std::wstring& path, const std::wstring& text)
	{
		// UTF-8 (BOM 없음), CRLF
		std::string utf8;
		const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
		utf8.resize(length > 0 ? length - 1 : 0);
		if (length > 1) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &utf8[0], length, nullptr, nullptr);
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file) return false;
		file << utf8;
		return true;
	}

	bool FileContains(const std::wstring& path, const char* text)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file) return false;
		const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		return content.find(text) != std::string::npos;
	}

	// 프로젝트 폴더에서 엔진 저장소로 가는 경로. 같은 드라이브면 상대 경로(..\..\), 아니면 절대 경로.
	std::wstring EngineRepoFromProject(const std::wstring& projectRoot, const std::wstring& repo)
	{
		std::error_code ec;
		const fs::path relative = fs::relative(repo, projectRoot, ec);
		if (ec || relative.empty()) return repo;
		std::wstring r = relative.wstring();
		if (r.empty() || r == L".") return L".\\";
		if (r.back() != L'\\') r += L'\\';
		return r;
	}

	// 2026-10-08 (B안): <Name>Scripts.vcxproj — 프로젝트 스크립트만 담은 DLL.
	// 엔진은 SherlockEngine.dll 이고 이 DLL 은 그 가져오기 라이브러리(x64\<구성>\SherlockEngine.lib)를 링크함.
	// 컴파일 설정(C++20, /utf-8, pch.h, Debug/Release 의 CRT)은 엔진과 같아야 함 — 경계 너머로 std::string 등을 넘기기 때문.
	std::wstring ScriptsProject(const Project& project, const std::wstring& repoFromProject)
	{
		const std::wstring name = project.GetScriptsProjectName();
		const std::wstring projectName = Wide(project.GetName());
		std::wstringstream s;
		s << L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
		s << L"<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\r\n";
		s << L"  <!-- " << kGeneratorMarker << L": 프로젝트 " << projectName << L" 의 스크립트 DLL. 에디터가 만들었고 다시 생성하면 덮어쓴다.\r\n";
		s << L"       빌드하면 Binaries\\<구성>\\" << name << L".dll 이 생기고, 켜져 있는 에디터가 1초 안에 다시 올린다(핫리로드). F5 는 엔진의 SherlockEditor.exe 로 이 프로젝트를 연다. -->\r\n";
		s << L"  <ItemGroup Label=\"ProjectConfigurations\">\r\n";
		for (const wchar_t* c : { L"Debug", L"Release" })
			s << L"    <ProjectConfiguration Include=\"" << c << L"|x64\">\r\n      <Configuration>" << c << L"</Configuration>\r\n      <Platform>x64</Platform>\r\n    </ProjectConfiguration>\r\n";
		s << L"  </ItemGroup>\r\n";
		s << L"  <PropertyGroup Label=\"Globals\">\r\n    <VCProjectVersion>17.0</VCProjectVersion>\r\n    <Keyword>Win32Proj</Keyword>\r\n";
		s << L"    <ProjectGuid>" << kScriptsGuid << L"</ProjectGuid>\r\n    <RootNamespace>" << name << L"</RootNamespace>\r\n";
		s << L"    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>\r\n";
		s << L"    <SherlockRepo>$(MSBuildProjectDirectory)\\" << repoFromProject << L"</SherlockRepo>\r\n";
		s << L"    <SherlockEngineDir>$(SherlockRepo)SherlockEngine\\</SherlockEngineDir>\r\n";
		s << L"    <VcpkgEnableManifest>true</VcpkgEnableManifest>\r\n    <VcpkgManifestRoot>$(SherlockRepo)</VcpkgManifestRoot>\r\n";
		// vcpkg 라이브러리는 헤더만 씀. 자동 링크를 켜면 ImGui 같은 정적 라이브러리가 이 DLL 에 한 벌 더 들어감.
		s << L"    <VcpkgAutoLink>false</VcpkgAutoLink>\r\n";
		s << L"  </PropertyGroup>\r\n";
		s << L"  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\r\n";
		for (const wchar_t* c : { L"Debug", L"Release" })
		{
			s << L"  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='" << c << L"|x64'\" Label=\"Configuration\">\r\n";
			s << L"    <ConfigurationType>DynamicLibrary</ConfigurationType>\r\n    <UseDebugLibraries>" << (wcscmp(c, L"Debug") == 0 ? L"true" : L"false") << L"</UseDebugLibraries>\r\n";
			s << L"    <PlatformToolset>v143</PlatformToolset>\r\n";
			if (wcscmp(c, L"Release") == 0) s << L"    <WholeProgramOptimization>true</WholeProgramOptimization>\r\n";
			s << L"    <CharacterSet>Unicode</CharacterSet>\r\n  </PropertyGroup>\r\n";
		}
		s << L"  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />\r\n";
		s << L"  <ImportGroup Label=\"ExtensionSettings\">\r\n  </ImportGroup>\r\n  <ImportGroup Label=\"Shared\">\r\n  </ImportGroup>\r\n";
		for (const wchar_t* c : { L"Debug", L"Release" })
		{
			s << L"  <ImportGroup Label=\"PropertySheets\" Condition=\"'$(Configuration)|$(Platform)'=='" << c << L"|x64'\">\r\n";
			s << L"    <Import Project=\"$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props\" Condition=\"exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')\" Label=\"LocalAppDataPlatform\" />\r\n";
			s << L"  </ImportGroup>\r\n";
		}
		s << L"  <PropertyGroup Label=\"UserMacros\" />\r\n";
		for (const wchar_t* c : { L"Debug", L"Release" })
		{
			s << L"  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='" << c << L"|x64'\">\r\n";
			s << L"    <OutDir>$(ProjectDir)Binaries\\$(Configuration)\\</OutDir>\r\n";
			s << L"    <IntDir>$(ProjectDir)Intermediate\\$(ProjectName)\\$(Configuration)\\</IntDir>\r\n";
			s << L"    <LocalDebuggerCommand>$(SherlockRepo)x64\\$(Configuration)\\SherlockEditor.exe</LocalDebuggerCommand>\r\n";
			s << L"    <LocalDebuggerCommandArguments>--project=\"$(ProjectDir)" << projectName << L".sherlock\"</LocalDebuggerCommandArguments>\r\n";
			s << L"    <LocalDebuggerWorkingDirectory>$(SherlockRepo)x64\\$(Configuration)\\</LocalDebuggerWorkingDirectory>\r\n";
			s << L"    <DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>\r\n  </PropertyGroup>\r\n";
		}
		for (const wchar_t* c : { L"Debug", L"Release" })
		{
			const bool debug = wcscmp(c, L"Debug") == 0;
			s << L"  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='" << c << L"|x64'\">\r\n    <ClCompile>\r\n";
			s << L"      <WarningLevel>Level3</WarningLevel>\r\n";
			if (!debug) s << L"      <FunctionLevelLinking>true</FunctionLevelLinking>\r\n      <IntrinsicFunctions>true</IntrinsicFunctions>\r\n";
			s << L"      <SDLCheck>true</SDLCheck>\r\n";
			s << L"      <PreprocessorDefinitions>" << (debug ? L"_DEBUG" : L"NDEBUG") << L";_WINDOWS;_USRDLL;%(PreprocessorDefinitions)</PreprocessorDefinitions>\r\n";
			s << L"      <ConformanceMode>true</ConformanceMode>\r\n      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>\r\n";
			// 언어 표준은 엔진과 같은 C++20 — Script.h 의 GetComponent 가 std::derived_from 콘셉트를 씀 (2026-09-30 에 생성기에서 빠뜨렸던 것).
			s << L"      <LanguageStandard>stdcpp20</LanguageStandard>\r\n      <PrecompiledHeader>Use</PrecompiledHeader>\r\n      <PrecompiledHeaderFile>pch.h</PrecompiledHeaderFile>\r\n";
			s << L"      <AdditionalIncludeDirectories>$(SherlockEngineDir);$(ProjectDir)Scripts;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>\r\n";
			s << L"    </ClCompile>\r\n    <Link>\r\n      <SubSystem>Windows</SubSystem>\r\n      <GenerateDebugInformation>true</GenerateDebugInformation>\r\n";
			s << L"      <AdditionalDependencies>SherlockEngine.lib;%(AdditionalDependencies)</AdditionalDependencies>\r\n";
			s << L"      <AdditionalLibraryDirectories>$(SherlockRepo)x64\\$(Configuration)\\;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>\r\n";
			s << L"    </Link>\r\n  </ItemDefinitionGroup>\r\n";
		}
		// Scripts\ 아래 하위 폴더까지 재귀(**). 실험용 스크립트를 Scripts\Tests\ 에 따로 모아 두기 위해서다 (QuaternionLookTest 가 첫 사용자).
		s << L"  <ItemGroup>\r\n    <ClCompile Include=\"Scripts\\**\\*.cpp\" />\r\n";
		s << L"    <ClCompile Include=\"$(SherlockEngineDir)pch.cpp\">\r\n      <PrecompiledHeader>Create</PrecompiledHeader>\r\n    </ClCompile>\r\n";
		s << L"  </ItemGroup>\r\n  <ItemGroup>\r\n    <ClInclude Include=\"Scripts\\**\\*.h\" />\r\n  </ItemGroup>\r\n";
		s << L"  <ItemGroup>\r\n    <None Include=\"Assets\\Scenes\\*.json\" />\r\n    <None Include=\"" << projectName << L".sherlock\" />\r\n  </ItemGroup>\r\n";
		// 엔진은 빌드 순서만 맞추고 링크는 위의 AdditionalDependencies 로 함. 에디터의 Build scripts 는 /p:BuildProjectReferences=false 로
		// 엔진을 건드리지 않음 — 엔진 DLL 은 그 에디터가 쓰는 중이라 다시 링크할 수 없음.
		s << L"  <ItemGroup>\r\n    <ProjectReference Include=\"$(SherlockEngineDir)SherlockEngine.vcxproj\">\r\n      <Project>" << kEngineGuid << L"</Project>\r\n";
		s << L"      <ReferenceOutputAssembly>false</ReferenceOutputAssembly>\r\n      <LinkLibraryDependencies>false</LinkLibraryDependencies>\r\n    </ProjectReference>\r\n  </ItemGroup>\r\n";
		s << L"  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\r\n  <ImportGroup Label=\"ExtensionTargets\">\r\n  </ImportGroup>\r\n</Project>\r\n";
		return s.str();
	}

	std::wstring Filters()
	{
		std::wstringstream s;
		s << L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\r\n";
		s << L"  <ItemGroup>\r\n    <Filter Include=\"Scripts\">\r\n      <UniqueIdentifier>{a0c2e1f0-5c11-4b3e-9a6f-1a2b3c4d5e11}</UniqueIdentifier>\r\n    </Filter>\r\n";
		s << L"    <Filter Include=\"Assets\">\r\n      <UniqueIdentifier>{a0c2e1f0-5c11-4b3e-9a6f-1a2b3c4d5e13}</UniqueIdentifier>\r\n    </Filter>\r\n  </ItemGroup>\r\n";
		s << L"  <ItemGroup>\r\n    <ClCompile Include=\"Scripts\\**\\*.cpp\">\r\n      <Filter>Scripts</Filter>\r\n    </ClCompile>\r\n  </ItemGroup>\r\n";
		s << L"  <ItemGroup>\r\n    <ClInclude Include=\"Scripts\\**\\*.h\">\r\n      <Filter>Scripts</Filter>\r\n    </ClInclude>\r\n  </ItemGroup>\r\n";
		s << L"  <ItemGroup>\r\n    <None Include=\"Assets\\Scenes\\*.json\">\r\n      <Filter>Assets</Filter>\r\n    </None>\r\n  </ItemGroup>\r\n</Project>\r\n";
		return s.str();
	}

	// 솔루션: 스크립트 DLL(시작 프로젝트 — F5 가 에디터를 띄움) + 엔진 DLL + 엔진 에디터. 엔진 두 프로젝트는 저장소의 파일을 그대로 참조함.
	std::wstring Solution(const Project& project, const std::wstring& repoFromProject)
	{
		const std::wstring scriptsName = project.GetScriptsProjectName();
		std::wstringstream s;
		s << L"\xFEFF";   // BOM: VS 가 만드는 sln 과 같게 맞춤
		s << L"\r\nMicrosoft Visual Studio Solution File, Format Version 12.00\r\n# Visual Studio Version 17\r\nVisualStudioVersion = 17.0.31903.59\r\nMinimumVisualStudioVersion = 10.0.40219.1\r\n";
		s << L"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"" << scriptsName << L"\", \"" << scriptsName << L".vcxproj\", \"" << kScriptsGuid << L"\"\r\nEndProject\r\n";
		s << L"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"SherlockEngine\", \"" << repoFromProject << L"SherlockEngine\\SherlockEngine.vcxproj\", \"" << kEngineGuid << L"\"\r\nEndProject\r\n";
		s << L"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"SherlockEditor\", \"" << repoFromProject << L"SherlockEditor\\SherlockEditor.vcxproj\", \"" << kEditorGuid << L"\"\r\nEndProject\r\n";
		s << L"Global\r\n\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\r\n\t\tDebug|x64 = Debug|x64\r\n\t\tRelease|x64 = Release|x64\r\n\tEndGlobalSection\r\n";
		s << L"\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\r\n";
		for (const wchar_t* guid : { kScriptsGuid, kEngineGuid, kEditorGuid })
			for (const wchar_t* c : { L"Debug", L"Release" })
				s << L"\t\t" << guid << L"." << c << L"|x64.ActiveCfg = " << c << L"|x64\r\n\t\t" << guid << L"." << c << L"|x64.Build.0 = " << c << L"|x64\r\n";
		s << L"\tEndGlobalSection\r\n\tGlobalSection(SolutionProperties) = preSolution\r\n\t\tHideSolutionNode = FALSE\r\n\tEndGlobalSection\r\nEndGlobal\r\n";
		return s.str();
	}
}

std::wstring ProjectGenerator::GetEngineRepoDir()
{
	if (!Paths::HasEngineSourceTree()) return L"";
	const std::wstring engine = Paths::GetEngineRoot();   // <repo>\SherlockEngine 폴더 (끝 백슬래시)
	const size_t slash = engine.find_last_of(L'\\', engine.size() - 2);
	return slash == std::wstring::npos ? engine : engine.substr(0, slash + 1);
}

bool ProjectGenerator::Generate(const Project& project, std::string& error)
{
	const std::wstring repo = GetEngineRepoDir();
	if (repo.empty()) { error = "engine source tree not found (cannot generate a solution without it)"; return false; }
	const std::wstring repoFromProject = EngineRepoFromProject(project.GetRoot(), repo);
	const std::wstring name = Wide(project.GetName());
	const std::wstring root = project.GetRoot();
	if (!WriteAll(root + project.GetScriptsProjectName() + L".vcxproj", ScriptsProject(project, repoFromProject))
		|| !WriteAll(root + project.GetScriptsProjectName() + L".vcxproj.filters", Filters())
		|| !WriteAll(project.GetSolutionPath(), Solution(project, repoFromProject)))
	{
		error = "cannot write project files in " + Log::ToUtf8(root.c_str());
		return false;
	}
	// 형식 1 의 exe 프로젝트(<Name>Editor.vcxproj, <Name>.vcxproj)는 이제 쓰지 않음. 생성기가 만든 표시가 있는 것만 지움 (손으로 만든 파일은 남김).
	for (const std::wstring& old : { root + name + L"Editor.vcxproj", root + name + L".vcxproj" })
	{
		if (!FileContains(old, kOldGeneratorMarkerUtf8)) continue;
		std::error_code ec;
		fs::remove(old, ec);
		fs::remove(old + L".filters", ec);
		Log::Info("옛 형식 프로젝트 파일 지움: %s", Log::ToUtf8(old.c_str()).c_str());
	}
	Log::Info("프로젝트 솔루션 생성: %s (스크립트 DLL %s, 엔진 저장소 %s)", Log::ToUtf8(project.GetSolutionPath().c_str()).c_str(),
		Log::ToUtf8(project.GetScriptsProjectName().c_str()).c_str(), Log::ToUtf8(repoFromProject.c_str()).c_str());
	return true;
}

bool ProjectGenerator::EnsureUpToDate(const Project& project)
{
	if (!project.IsLoaded() || GetEngineRepoDir().empty()) return false;
	if (FileContains(project.GetRoot() + project.GetScriptsProjectName() + L".vcxproj", kGeneratorMarkerUtf8)) return false;
	std::string error;
	if (!Generate(project, error))
	{
		Log::Warn("프로젝트 솔루션을 스크립트 DLL 형식으로 바꾸지 못함: %s", error.c_str());
		return false;
	}
	return true;
}

bool ProjectGenerator::RunMsBuild(const std::wstring& solution, const std::wstring& target, const wchar_t* configuration, const std::wstring& logFile, std::string& error, const std::wstring& extraArguments)
{
	wchar_t programFiles[MAX_PATH] = {};
	ExpandEnvironmentStringsW(L"%ProgramFiles(x86)%", programFiles, MAX_PATH);
	const std::wstring vswhere = std::wstring(programFiles) + L"\\Microsoft Visual Studio\\Installer\\vswhere.exe";
	if (GetFileAttributesW(vswhere.c_str()) == INVALID_FILE_ATTRIBUTES) { error = "vswhere.exe not found (Visual Studio installed?)"; return false; }

	std::error_code ec;
	fs::create_directories(fs::path(logFile).parent_path(), ec);
	// cmd 에서 두 단계로 실행: vswhere 로 MSBuild 경로를 찾고 그 경로로 빌드. 창은 띄우지 않음.
	std::wstring command = L"cmd.exe /c \"for /f \"usebackq delims=\" %m in (`\"" + vswhere + L"\" -latest -requires Microsoft.Component.MSBuild -find MSBuild\\**\\Bin\\MSBuild.exe`) do \"%m\" \""
		+ solution + L"\" /t:" + target + L" /p:Configuration=" + configuration + L" /p:Platform=x64 /m /v:m /nologo " + extraArguments + L" > \"" + logFile + L"\" 2>&1\"";
	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = {};
	std::vector<wchar_t> buffer(command.begin(), command.end());
	buffer.push_back(L'\0');
	if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
	{
		error = "cannot start MSBuild";
		return false;
	}
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD exitCode = 1;
	GetExitCodeProcess(pi.hProcess, &exitCode);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	if (exitCode != 0)
	{
		std::ifstream log(logFile, std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
		error = std::format("MSBuild failed (exit {}). See {}", exitCode, Log::ToUtf8(logFile.c_str()));   // C++20 std::format
		Log::Error("MSBuild 실패 (%s %s)\n%s", Log::ToUtf8(solution.c_str()).c_str(), Log::ToUtf8(target.c_str()).c_str(), text.substr(0, 4000).c_str());
		return false;
	}
	Log::Info("MSBuild 완료: %s /t:%s (%s)", Log::ToUtf8(solution.c_str()).c_str(), Log::ToUtf8(target.c_str()).c_str(), Log::ToUtf8(configuration).c_str());
	return true;
}

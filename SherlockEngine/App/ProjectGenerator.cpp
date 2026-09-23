#include "pch.h"
#include "App/ProjectGenerator.h"
#include "Core/Project.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
	const wchar_t* kEngineGuid = L"{DCC06A61-7D5A-44F5-A959-3D9367E30EE7}";   // SherlockEngine.vcxproj 의 ProjectGuid
	const wchar_t* kEditorGuid = L"{6F2E9A10-3C4D-4B5E-9F60-1E2D3C4B5A6A}";   // 생성 프로젝트의 고정 GUID (엔진 솔루션의 것과 다르다)
	const wchar_t* kGameGuid = L"{7A3F0B21-4D5E-4C6F-A071-2F3E4D5C6B7B}";

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

	// 프로젝트 폴더에서 엔진 저장소로 가는 경로. 같은 드라이브면 상대(..\..\), 아니면 절대.
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

	std::wstring ExeProject(const Project& project, bool editor, const std::wstring& repoFromProject)
	{
		const std::wstring name = editor ? project.GetEditorProjectName() : Wide(project.GetName());
		const wchar_t* guid = editor ? kEditorGuid : kGameGuid;
		std::wstringstream s;
		s << L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
		s << L"<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\r\n";
		s << L"  <!-- SherlockEngine 프로젝트 " << Wide(project.GetName()) << L" — 에디터의 ProjectGenerator 가 만들었다. 손으로 고쳐도 되지만 다시 생성하면 덮어쓴다. -->\r\n";
		s << L"  <ItemGroup Label=\"ProjectConfigurations\">\r\n";
		for (const wchar_t* c : { L"Debug", L"Release" })
			s << L"    <ProjectConfiguration Include=\"" << c << L"|x64\">\r\n      <Configuration>" << c << L"</Configuration>\r\n      <Platform>x64</Platform>\r\n    </ProjectConfiguration>\r\n";
		s << L"  </ItemGroup>\r\n";
		s << L"  <PropertyGroup Label=\"Globals\">\r\n    <VCProjectVersion>17.0</VCProjectVersion>\r\n    <Keyword>Win32Proj</Keyword>\r\n";
		s << L"    <ProjectGuid>" << guid << L"</ProjectGuid>\r\n    <RootNamespace>" << name << L"</RootNamespace>\r\n";
		s << L"    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>\r\n";
		s << L"    <SherlockRepo>$(MSBuildProjectDirectory)\\" << repoFromProject << L"</SherlockRepo>\r\n";
		s << L"    <SherlockEngineDir>$(SherlockRepo)SherlockEngine\\</SherlockEngineDir>\r\n";
		s << L"    <VcpkgEnableManifest>true</VcpkgEnableManifest>\r\n    <VcpkgManifestRoot>$(SherlockRepo)</VcpkgManifestRoot>\r\n";
		s << L"  </PropertyGroup>\r\n";
		s << L"  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\r\n";
		for (const wchar_t* c : { L"Debug", L"Release" })
		{
			s << L"  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='" << c << L"|x64'\" Label=\"Configuration\">\r\n";
			s << L"    <ConfigurationType>Application</ConfigurationType>\r\n    <UseDebugLibraries>" << (wcscmp(c, L"Debug") == 0 ? L"true" : L"false") << L"</UseDebugLibraries>\r\n";
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
			s << L"    <LocalDebuggerWorkingDirectory>$(ProjectDir)</LocalDebuggerWorkingDirectory>\r\n";
			s << L"    <LocalDebuggerCommandArguments>--project=$(ProjectDir)</LocalDebuggerCommandArguments>\r\n";
			s << L"    <DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>\r\n  </PropertyGroup>\r\n";
		}
		for (const wchar_t* c : { L"Debug", L"Release" })
		{
			const bool debug = wcscmp(c, L"Debug") == 0;
			s << L"  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='" << c << L"|x64'\">\r\n    <ClCompile>\r\n";
			s << L"      <WarningLevel>Level3</WarningLevel>\r\n";
			if (!debug) s << L"      <FunctionLevelLinking>true</FunctionLevelLinking>\r\n      <IntrinsicFunctions>true</IntrinsicFunctions>\r\n";
			s << L"      <SDLCheck>true</SDLCheck>\r\n";
			s << L"      <PreprocessorDefinitions>" << (debug ? L"_DEBUG" : L"NDEBUG") << L";_CONSOLE;SHERLOCK_PROJECT_NAME=\"" << Wide(project.GetName()) << L"\";%(PreprocessorDefinitions)</PreprocessorDefinitions>\r\n";
			s << L"      <ConformanceMode>true</ConformanceMode>\r\n      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>\r\n";
			s << L"      <LanguageStandard>stdcpp17</LanguageStandard>\r\n      <PrecompiledHeader>Use</PrecompiledHeader>\r\n      <PrecompiledHeaderFile>pch.h</PrecompiledHeaderFile>\r\n";
			s << L"      <AdditionalIncludeDirectories>$(SherlockEngineDir);$(ProjectDir)Scripts;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>\r\n";
			s << L"    </ClCompile>\r\n    <Link>\r\n      <SubSystem>Windows</SubSystem>\r\n      <GenerateDebugInformation>true</GenerateDebugInformation>\r\n";
			s << L"      <AdditionalOptions>/WHOLEARCHIVE:SherlockEngine.lib %(AdditionalOptions)</AdditionalOptions>\r\n";
			s << L"    </Link>\r\n  </ItemDefinitionGroup>\r\n";
		}
		s << L"  <ItemGroup>\r\n";
		if (editor)
		{
			s << L"    <ClCompile Include=\"$(SherlockRepo)SherlockEditor\\EditorMain.cpp\" />\r\n";
			for (const std::wstring& source : ProjectGenerator::GetEditorSources())
				s << L"    <ClCompile Include=\"$(SherlockEngineDir)App\\" << source << L".cpp\" />\r\n";
		}
		else
		{
			s << L"    <ClCompile Include=\"$(SherlockRepo)SherlockGame\\GameMain.cpp\" />\r\n";
		}
		s << L"    <ClCompile Include=\"Scripts\\*.cpp\" />\r\n";
		s << L"    <ClCompile Include=\"$(SherlockEngineDir)pch.cpp\">\r\n      <PrecompiledHeader>Create</PrecompiledHeader>\r\n    </ClCompile>\r\n";
		s << L"  </ItemGroup>\r\n  <ItemGroup>\r\n    <ClInclude Include=\"Scripts\\*.h\" />\r\n";
		if (editor)
			for (const std::wstring& source : ProjectGenerator::GetEditorSources())
				s << L"    <ClInclude Include=\"$(SherlockEngineDir)App\\" << source << L".h\" />\r\n";
		s << L"  </ItemGroup>\r\n";
		s << L"  <ItemGroup>\r\n    <None Include=\"Assets\\Scenes\\*.json\" />\r\n    <None Include=\"" << Wide(project.GetName()) << L".sherlock\" />\r\n  </ItemGroup>\r\n";
		s << L"  <ItemGroup>\r\n    <ProjectReference Include=\"$(SherlockEngineDir)SherlockEngine.vcxproj\">\r\n      <Project>" << kEngineGuid << L"</Project>\r\n    </ProjectReference>\r\n  </ItemGroup>\r\n";
		s << L"  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\r\n  <ImportGroup Label=\"ExtensionTargets\">\r\n  </ImportGroup>\r\n</Project>\r\n";
		return s.str();
	}

	std::wstring Filters(bool editor)
	{
		std::wstringstream s;
		s << L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\r\n";
		s << L"  <ItemGroup>\r\n    <Filter Include=\"Scripts\">\r\n      <UniqueIdentifier>{a0c2e1f0-5c11-4b3e-9a6f-1a2b3c4d5e11}</UniqueIdentifier>\r\n    </Filter>\r\n";
		s << L"    <Filter Include=\"Engine Editor\">\r\n      <UniqueIdentifier>{a0c2e1f0-5c11-4b3e-9a6f-1a2b3c4d5e12}</UniqueIdentifier>\r\n    </Filter>\r\n";
		s << L"    <Filter Include=\"Assets\">\r\n      <UniqueIdentifier>{a0c2e1f0-5c11-4b3e-9a6f-1a2b3c4d5e13}</UniqueIdentifier>\r\n    </Filter>\r\n  </ItemGroup>\r\n";
		s << L"  <ItemGroup>\r\n    <ClCompile Include=\"Scripts\\*.cpp\">\r\n      <Filter>Scripts</Filter>\r\n    </ClCompile>\r\n";
		if (editor)
			for (const std::wstring& source : ProjectGenerator::GetEditorSources())
				s << L"    <ClCompile Include=\"$(SherlockEngineDir)App\\" << source << L".cpp\">\r\n      <Filter>Engine Editor</Filter>\r\n    </ClCompile>\r\n";
		s << L"  </ItemGroup>\r\n  <ItemGroup>\r\n    <ClInclude Include=\"Scripts\\*.h\">\r\n      <Filter>Scripts</Filter>\r\n    </ClInclude>\r\n";
		if (editor)
			for (const std::wstring& source : ProjectGenerator::GetEditorSources())
				s << L"    <ClInclude Include=\"$(SherlockEngineDir)App\\" << source << L".h\">\r\n      <Filter>Engine Editor</Filter>\r\n    </ClInclude>\r\n";
		s << L"  </ItemGroup>\r\n  <ItemGroup>\r\n    <None Include=\"Assets\\Scenes\\*.json\">\r\n      <Filter>Assets</Filter>\r\n    </None>\r\n  </ItemGroup>\r\n</Project>\r\n";
		return s.str();
	}

	std::wstring Solution(const Project& project, const std::wstring& repoFromProject)
	{
		const std::wstring name = Wide(project.GetName());
		const std::wstring editorName = project.GetEditorProjectName();
		std::wstringstream s;
		s << L"\xFEFF";   // BOM: VS 가 만드는 sln 과 같게
		s << L"\r\nMicrosoft Visual Studio Solution File, Format Version 12.00\r\n# Visual Studio Version 17\r\nVisualStudioVersion = 17.0.31903.59\r\nMinimumVisualStudioVersion = 10.0.40219.1\r\n";
		s << L"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"" << editorName << L"\", \"" << editorName << L".vcxproj\", \"" << kEditorGuid << L"\"\r\nEndProject\r\n";
		s << L"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"" << name << L"\", \"" << name << L".vcxproj\", \"" << kGameGuid << L"\"\r\nEndProject\r\n";
		s << L"Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"SherlockEngine\", \"" << repoFromProject << L"SherlockEngine\\SherlockEngine.vcxproj\", \"" << kEngineGuid << L"\"\r\nEndProject\r\n";
		s << L"Global\r\n\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\r\n\t\tDebug|x64 = Debug|x64\r\n\t\tRelease|x64 = Release|x64\r\n\tEndGlobalSection\r\n";
		s << L"\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\r\n";
		for (const wchar_t* guid : { kEditorGuid, kGameGuid, kEngineGuid })
			for (const wchar_t* c : { L"Debug", L"Release" })
				s << L"\t\t" << guid << L"." << c << L"|x64.ActiveCfg = " << c << L"|x64\r\n\t\t" << guid << L"." << c << L"|x64.Build.0 = " << c << L"|x64\r\n";
		s << L"\tEndGlobalSection\r\n\tGlobalSection(SolutionProperties) = preSolution\r\n\t\tHideSolutionNode = FALSE\r\n\tEndGlobalSection\r\nEndGlobal\r\n";
		return s.str();
	}
}

const std::vector<std::wstring>& ProjectGenerator::GetEditorSources()
{
	static const std::vector<std::wstring> sources = { L"TestApp", L"Editor", L"ContentBrowser", L"ScriptCreator", L"GameBuilder", L"DebugUI", L"ProjectGenerator", L"ProjectLauncher" };
	return sources;
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
	if (!WriteAll(root + project.GetEditorProjectName() + L".vcxproj", ExeProject(project, true, repoFromProject))
		|| !WriteAll(root + project.GetEditorProjectName() + L".vcxproj.filters", Filters(true))
		|| !WriteAll(root + name + L".vcxproj", ExeProject(project, false, repoFromProject))
		|| !WriteAll(root + name + L".vcxproj.filters", Filters(false))
		|| !WriteAll(project.GetSolutionPath(), Solution(project, repoFromProject)))
	{
		error = "cannot write project files in " + Log::ToUtf8(root.c_str());
		return false;
	}
	Log::Info("프로젝트 솔루션 생성: %s (엔진 저장소 %s)", Log::ToUtf8(project.GetSolutionPath().c_str()).c_str(), Log::ToUtf8(repoFromProject.c_str()).c_str());
	return true;
}

bool ProjectGenerator::RunMsBuild(const std::wstring& solution, const std::wstring& target, const wchar_t* configuration, const std::wstring& logFile, std::string& error)
{
	wchar_t programFiles[MAX_PATH] = {};
	ExpandEnvironmentStringsW(L"%ProgramFiles(x86)%", programFiles, MAX_PATH);
	const std::wstring vswhere = std::wstring(programFiles) + L"\\Microsoft Visual Studio\\Installer\\vswhere.exe";
	if (GetFileAttributesW(vswhere.c_str()) == INVALID_FILE_ATTRIBUTES) { error = "vswhere.exe not found (Visual Studio installed?)"; return false; }

	std::error_code ec;
	fs::create_directories(fs::path(logFile).parent_path(), ec);
	// cmd 로 두 단계: vswhere 가 찾은 MSBuild 경로로 빌드. 창은 띄우지 않는다.
	std::wstring command = L"cmd.exe /c \"for /f \"usebackq delims=\" %m in (`\"" + vswhere + L"\" -latest -requires Microsoft.Component.MSBuild -find MSBuild\\**\\Bin\\MSBuild.exe`) do \"%m\" \""
		+ solution + L"\" /t:" + target + L" /p:Configuration=" + configuration + L" /p:Platform=x64 /m /v:m /nologo > \"" + logFile + L"\" 2>&1\"";
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
		error = "MSBuild failed (exit " + std::to_string(exitCode) + "). See " + Log::ToUtf8(logFile.c_str());
		Log::Error("MSBuild 실패 (%s %s)\n%s", Log::ToUtf8(solution.c_str()).c_str(), Log::ToUtf8(target.c_str()).c_str(), text.substr(0, 4000).c_str());
		return false;
	}
	Log::Info("MSBuild 완료: %s /t:%s (%s)", Log::ToUtf8(solution.c_str()).c_str(), Log::ToUtf8(target.c_str()).c_str(), Log::ToUtf8(configuration).c_str());
	return true;
}

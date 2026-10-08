#include "pch.h"
#include "App/GameBuilder.h"
#include "App/ProjectGenerator.h"
#include "Core/ScriptModule.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <shellapi.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <chrono>
#include <set>

namespace fs = std::filesystem;

namespace
{
	std::string Utf8(const std::wstring& s) { return Log::ToUtf8(s.c_str()); }
	std::wstring Wide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
		return wide;
	}

	bool CopyOne(const fs::path& from, const fs::path& to, int& count, std::string& error)
	{
		std::error_code ec;
		fs::create_directories(to.parent_path(), ec);
		fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
		if (ec) { error = "copy failed: " + Utf8(from.wstring()) + " (" + ec.message() + ")"; return false; }
		++count;
		return true;
	}
	bool CopyTree(const fs::path& from, const fs::path& to, int& count, std::string& error, bool required = true)
	{
		std::error_code ec;
		if (!fs::is_directory(from, ec))
		{
			if (required) error = "missing folder: " + Utf8(from.wstring());
			return !required;
		}
		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(from, ec))
		{
			if (entry.is_directory(ec)) continue;
			const fs::path relative = fs::relative(entry.path(), from, ec);
			if (!CopyOne(entry.path(), to / relative, count, error)) return false;
		}
		return true;
	}

	// 시작 씬이 참조하는 에셋을 수집함: 모델 경로의 앞 두 단계(Models\Sponza)와 재질의 텍스처 이름.
	void CollectReferences(const fs::path& scenePath, std::set<std::wstring>& modelFolders, std::set<std::wstring>& textures)
	{
		std::ifstream file(scenePath, std::ios::binary);
		if (!file) return;
		nlohmann::json root;
		try { root = nlohmann::json::parse(file); }
		catch (...) { return; }
		for (const auto& mesh : root.value("meshes", nlohmann::json::array()))
		{
			const std::string path = mesh.value("path", "");
			if (path.empty()) continue;
			std::wstring wide = Wide(path);
			for (wchar_t& c : wide) if (c == L'/') c = L'\\';
			const size_t first = wide.find(L'\\');
			const size_t second = first == std::wstring::npos ? std::wstring::npos : wide.find(L'\\', first + 1);
			modelFolders.insert(second == std::wstring::npos ? wide : wide.substr(0, second));
		}
		for (const auto& material : root.value("materials", nlohmann::json::array()))
		{
			for (const char* key : { "albedoTexture", "normalTexture", "metallicRoughnessTexture", "occlusionTexture", "emissiveTexture" })
			{
				const std::string name = material.value(key, "");
				if (name.empty() || name.rfind("builtin:", 0) == 0) continue;
				if (name.rfind("asset:", 0) == 0) textures.insert(Wide(name.substr(6)));
				else if (name.find('/') == std::string::npos) textures.insert(L"Textures\\" + Wide(name));
			}
		}
		// 2026-10-08: 스카이박스와 IBL 맵. 재질 텍스처와 같은 이름 규칙. 큐브 DDS·LUT 파일이거나 면 이미지 6장이 든 폴더임 (복사 루프가 둘 다 처리함).
		const nlohmann::json env = root.value("environment", nlohmann::json());
		if (env.is_object())
		{
			for (const char* key : { "skybox", "irradiance", "specular", "brdfLut" })
			{
				const std::string name = env.value(key, "");
				if (name.empty() || name.rfind("builtin:", 0) == 0) continue;
				if (name.rfind("asset:", 0) == 0) textures.insert(Wide(name.substr(6)));
				else textures.insert(L"Textures\\" + Wide(name));
			}
		}
	}

	// 상대 경로의 에셋 폴더를 프로젝트 → 엔진 순으로 찾아 복사함 (겹치면 프로젝트 것이 우선함).
	bool CopyAssetTree(const std::wstring& relative, const fs::path& outAssets, int& count, std::string& error)
	{
		bool any = false;
		const std::wstring engine = Paths::GetEngineAssetRoot();
		std::error_code ec;
		if (fs::is_directory(fs::path(engine) / relative, ec)) { if (!CopyTree(fs::path(engine) / relative, outAssets / relative, count, error)) return false; any = true; }
		if (Paths::HasProject())
		{
			const fs::path project = fs::path(Paths::GetAssetRoot()) / relative;
			if (fs::is_directory(project, ec)) { if (!CopyTree(project, outAssets / relative, count, error)) return false; any = true; }
		}
		if (!any) error = "missing folder: " + Utf8(relative);
		return any;
	}
}

bool GameBuilder::IsAvailable()
{
	return Paths::HasProject();
}

std::wstring GameBuilder::GetBuildRoot()
{
	return (Paths::HasProject() ? Paths::GetProjectRoot() : Paths::GetExecutableDir()) + L"Build\\";
}

std::vector<std::string> GameBuilder::ListScenes()
{
	std::vector<std::string> scenes;
	std::error_code ec;
	for (const fs::directory_entry& entry : fs::directory_iterator(Paths::GetAssetRoot() + L"Scenes", ec))
	{
		if (entry.is_regular_file(ec) && entry.path().extension() == L".json") scenes.push_back(Utf8(entry.path().filename().wstring()));
	}
	std::sort(scenes.begin(), scenes.end());
	return scenes;
}

GameBuilder::Result GameBuilder::Build(const Options& options)
{
	Result result;
	const auto started = std::chrono::steady_clock::now();
	if (options.name.empty() || options.startScene.empty()) { result.message = "name and start scene are required"; return result; }
	for (char c : options.name) if (c == '\\' || c == '/' || c == ':' || c == '"') { result.message = "invalid game name"; return result; }
	if (!Paths::HasProject()) { result.message = "no project is open"; return result; }

	// 2026-10-08 (B안): 런타임 = SherlockGame.exe + SherlockEngine.dll(+ vcpkg DLL) — 모든 프로젝트가 같은 exe 를 씀 — 와 프로젝트의 <이름>Scripts.dll.
	// Release 를 켰으면 엔진 솔루션의 SherlockGame 과 프로젝트의 스크립트 DLL 을 Release 로 빌드한 뒤 Release 폴더에서 가져옴.
	const std::wstring buildLog = GetBuildRoot() + L"msbuild.log";
	std::wstring runtimeDir = Paths::GetExecutableDir();
	const wchar_t* configuration = ScriptModule::GetConfiguration();
	if (options.releaseBuild)
	{
		configuration = L"Release";
		// 이 에디터가 Release 면 엔진 Release DLL 은 지금 쓰는 중이고 이미 빌드돼 있음 (다시 링크할 수 없음)
		if (wcscmp(ScriptModule::GetConfiguration(), L"Release") != 0)
		{
			Log::Info("게임 빌드: MSBuild SherlockGame (Release|x64) …");
			if (!ProjectGenerator::RunMsBuild(ProjectGenerator::GetEngineRepoDir() + L"SherlockEngine.sln", L"SherlockGame", L"Release", buildLog, result.message)) return result;
		}
		// 이 exe 가 x64\Debug 에 있으면 같은 위치의 Release 폴더를 씀
		const size_t debug = runtimeDir.rfind(L"\\Debug\\");
		if (debug != std::wstring::npos) runtimeDir = runtimeDir.substr(0, debug) + L"\\Release\\";
	}
	std::error_code ec;
	const fs::path runtimeExe = fs::path(runtimeDir) / L"SherlockGame.exe";
	if (!fs::exists(runtimeExe, ec)) { result.message = "game runtime not found in " + Utf8(runtimeDir) + " (build SherlockGame in SherlockEngine.sln)"; return result; }

	// 스크립트 DLL: <프로젝트>\Binaries\<구성>\<이름>Scripts.dll. Release 이거나 아직 없으면 먼저 빌드함 (엔진은 위에서 빌드했으므로 BuildProjectReferences=false).
	const std::wstring scriptsName = Wide(options.projectName) + L"Scripts";
	const fs::path scriptsDll = fs::path(Paths::GetProjectRoot()) / L"Binaries" / configuration / (scriptsName + L".dll");
	size_t scriptSources = 0;
	for (const fs::directory_entry& entry : fs::recursive_directory_iterator(Paths::GetProjectRoot() + L"Scripts", ec))
		if (entry.is_regular_file(ec) && entry.path().extension() == L".cpp") ++scriptSources;
	if (scriptSources > 0 && (options.releaseBuild || !fs::exists(scriptsDll, ec)))
	{
		if (options.projectSolution.empty() || !fs::exists(options.projectSolution, ec)) { result.message = "project solution not found: " + Utf8(options.projectSolution); return result; }
		Log::Info("게임 빌드: MSBuild %s (%s|x64) …", Utf8(scriptsName).c_str(), Utf8(configuration).c_str());
		if (!ProjectGenerator::RunMsBuild(options.projectSolution, scriptsName, configuration, buildLog, result.message, L"/p:BuildProjectReferences=false")) return result;
	}

	const fs::path out = fs::path(GetBuildRoot()) / Wide(options.name);
	result.outputDir = out.wstring() + L"\\";
	fs::create_directories(out, ec);

	// 1. 런타임: exe(게임 이름으로) + DLL (SherlockEngine.dll, vcpkg 가 둔 DirectXTex.dll 등)
	if (!CopyOne(runtimeExe, out / (Wide(options.name) + L".exe"), result.filesCopied, result.message)) return result;
	for (const fs::directory_entry& entry : fs::directory_iterator(runtimeDir, ec))
	{
		if (entry.is_regular_file(ec) && entry.path().extension() == L".dll")
			if (!CopyOne(entry.path(), out / entry.path().filename(), result.filesCopied, result.message)) return result;
	}
	// 스크립트 DLL 은 <게임 이름>Scripts.dll 로 둠 — 패키지의 .sherlock 이름이 게임 이름이고, 런타임은 exe 옆의 <프로젝트 이름>Scripts.dll 을 찾음 (AppBase::LoadProjectScripts).
	if (fs::exists(scriptsDll, ec))
	{
		if (!CopyOne(scriptsDll, out / (Wide(options.name) + L"Scripts.dll"), result.filesCopied, result.message)) return result;
	}
	else if (scriptSources > 0) { result.message = "scripts DLL not found: " + Utf8(scriptsDll.wstring()); return result; }
	// 2. 셰이더: exe 옆의 .cso + 엔진 루트의 .hlsl 소스 (런타임 컴파일 폴백)
	CopyTree(fs::path(runtimeDir) / L"Shaders", out / L"Shaders", result.filesCopied, result.message, false);
	if (!CopyTree(fs::path(Paths::GetEngineRoot()) / L"Shaders", out / L"Shaders", result.filesCopied, result.message)) return result;

	// 3. 에셋: 엔진 콘텐츠 위에 프로젝트 콘텐츠를 덮어씀 (런타임의 경로 해석 순서와 같음). 패키지 안에서는 둘이 한 폴더로 합쳐짐.
	const fs::path outAssets = out / L"Assets";
	const fs::path scenePath = fs::path(Paths::GetAssetRoot()) / L"Scenes" / Wide(options.startScene);
	if (!fs::exists(scenePath, ec)) { result.message = "start scene not found: " + options.startScene; return result; }
	if (options.copyAllAssets)
	{
		if (!CopyTree(Paths::GetEngineAssetRoot(), outAssets, result.filesCopied, result.message)) return result;
		if (!CopyTree(Paths::GetAssetRoot(), outAssets, result.filesCopied, result.message)) return result;
	}
	else
	{
		if (!CopyAssetTree(L"Config", outAssets, result.filesCopied, result.message)) return result;
		if (!CopyOne(scenePath, outAssets / L"Scenes" / Wide(options.startScene), result.filesCopied, result.message)) return result;
		std::set<std::wstring> modelFolders, textures;
		CollectReferences(scenePath, modelFolders, textures);
		for (const std::wstring& folder : modelFolders)
			if (!CopyAssetTree(folder, outAssets, result.filesCopied, result.message)) return result;
		for (const std::wstring& texture : textures)
		{
			const std::wstring found = Paths::GetAssetPath(texture.c_str());
			if (fs::is_directory(found, ec))   // 2026-10-08: 스카이박스 면 이미지 폴더
			{
				if (!CopyTree(found, outAssets / texture, result.filesCopied, result.message)) return result;
			}
			else if (fs::exists(found, ec) && !CopyOne(found, outAssets / texture, result.filesCopied, result.message)) return result;
		}
		CopyAssetTree(L"Textures", outAssets, result.filesCopied, result.message);   // 이름만으로 참조되는 기본 텍스처(bumps_normal 등)를 위해 전부 복사
	}

	// 4. engine.ini: [game] startScene / title 을 빌드 사본에 씀 (원본은 건드리지 않음).
	{
		const fs::path ini = outAssets / L"Config" / L"engine.ini";
		std::ifstream in(ini, std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		in.close();
		text += "\r\n; --- Build Game (" + options.name + ") ---\r\n[game]\r\nstartScene = " + options.startScene + "\r\ntitle = " + options.name + "\r\n";
		fs::create_directories(ini.parent_path(), ec);
		std::ofstream outIni(ini, std::ios::binary | std::ios::trunc);
		outIni << text;   // 같은 키가 앞에 있어도 마지막 값이 적용됨 (Config 는 키 → 값 맵)
	}
	// 5. 프로젝트 파일 사본: 런타임이 exe 옆에서 이 파일을 찾아 exe 폴더를 프로젝트 루트로 잡음 (engineRoot 가 비어 있으므로 모든 경로가 exe 옆을 가리킴).
	{
		std::ofstream sherlock(out / (Wide(options.name) + L".sherlock"), std::ios::binary | std::ios::trunc);
		sherlock << "{\n  \"name\": \"" << options.name << "\",\n  \"startScene\": \"" << options.startScene << "\",\n  \"engineRoot\": \"\",\n  \"format\": 1\n}\n";
		++result.filesCopied;
	}

	result.ok = true;
	result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	result.message = std::format("built {} ({} files, {:.0f} s)", options.name, result.filesCopied, result.seconds);   // C++20 std::format
	Log::Info("게임 빌드: %s → %s (%d 파일, %.1f 초)", options.name.c_str(), Utf8(result.outputDir).c_str(), result.filesCopied, result.seconds);
	if (options.openFolder) ShellExecuteW(nullptr, L"explore", result.outputDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return result;
}

#include "pch.h"
#include "App/GameBuilder.h"
#include "App/ProjectGenerator.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <shellapi.h>
#include <filesystem>
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

	// 시작 씬이 참조하는 에셋: 모델 경로의 첫 두 단계(Models\Sponza), 재질 텍스처 이름.
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
			for (const char* key : { "albedoTexture", "normalTexture" })
			{
				const std::string name = material.value(key, "");
				if (name.empty() || name.rfind("builtin:", 0) == 0) continue;
				if (name.rfind("asset:", 0) == 0) textures.insert(Wide(name.substr(6)));
				else if (name.find('/') == std::string::npos) textures.insert(L"Textures\\" + Wide(name));
			}
		}
	}

	// 상대 경로의 에셋 폴더를 프로젝트 → 엔진 순으로 찾아 복사 (프로젝트 것이 이긴다).
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

	// 런타임 exe: 이 에디터 옆의 <프로젝트>.exe (프로젝트 솔루션) 또는 SherlockGame.exe (엔진 솔루션의 Sample).
	// Release 를 켰으면 먼저 빌드하고 Release 폴더에서 가져온다.
	const std::wstring projectExe = Wide(options.projectName) + L".exe";
	std::wstring runtimeDir = Paths::GetExecutableDir();
	if (options.releaseBuild)
	{
		std::wstring solution = options.projectSolution;
		std::wstring target = Wide(options.projectName);
		if (solution.empty() || GetFileAttributesW(solution.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			// 엔진 솔루션의 Sample: 엔진 저장소의 SherlockEngine.sln, 타깃 SherlockGame
			solution = ProjectGenerator::GetEngineRepoDir() + L"SherlockEngine.sln";
			target = L"SherlockGame";
		}
		Log::Info("게임 빌드: MSBuild Release|x64 (%s) …", Utf8(target).c_str());
		if (!ProjectGenerator::RunMsBuild(solution, target, L"Release", GetBuildRoot() + L"msbuild.log", result.message)) return result;
		// 이 exe 가 x64\Debug 또는 Binaries\Debug 에 있으면 그 옆의 Release
		const size_t debug = runtimeDir.rfind(L"\\Debug\\");
		if (debug != std::wstring::npos) runtimeDir = runtimeDir.substr(0, debug) + L"\\Release\\";
	}
	std::error_code ec;
	fs::path runtimeExe = fs::path(runtimeDir) / projectExe;
	if (!fs::exists(runtimeExe, ec)) runtimeExe = fs::path(runtimeDir) / L"SherlockGame.exe";
	if (!fs::exists(runtimeExe, ec) && !options.releaseBuild && !options.projectSolution.empty() && GetFileAttributesW(options.projectSolution.c_str()) != INVALID_FILE_ATTRIBUTES)
	{
		// 프로젝트 솔루션의 게임 타깃이 아직 빌드되지 않았다 (에디터만 빌드한 경우). Debug 로 한 번 빌드해 둔다.
		Log::Info("게임 빌드: %s.exe 가 없어 MSBuild Debug|x64 (%s) 를 먼저 …", options.projectName.c_str(), options.projectName.c_str());
		if (!ProjectGenerator::RunMsBuild(options.projectSolution, Wide(options.projectName), L"Debug", GetBuildRoot() + L"msbuild.log", result.message)) return result;
		runtimeExe = fs::path(runtimeDir) / projectExe;
	}
	if (!fs::exists(runtimeExe, ec)) { result.message = "game runtime not found in " + Utf8(runtimeDir) + " (build the " + options.projectName + " / SherlockGame project)"; return result; }

	const fs::path out = fs::path(GetBuildRoot()) / Wide(options.name);
	result.outputDir = out.wstring() + L"\\";
	fs::create_directories(out, ec);

	// 1. 런타임: exe(게임 이름으로) + DLL (vcpkg 가 exe 옆에 둔 DirectXTex.dll 등)
	if (!CopyOne(runtimeExe, out / (Wide(options.name) + L".exe"), result.filesCopied, result.message)) return result;
	for (const fs::directory_entry& entry : fs::directory_iterator(runtimeDir, ec))
	{
		if (entry.is_regular_file(ec) && entry.path().extension() == L".dll")
			if (!CopyOne(entry.path(), out / entry.path().filename(), result.filesCopied, result.message)) return result;
	}
	// 2. 셰이더: exe 옆의 .cso + 엔진 루트의 .hlsl 소스 (런타임 컴파일 폴백)
	CopyTree(fs::path(runtimeDir) / L"Shaders", out / L"Shaders", result.filesCopied, result.message, false);
	if (!CopyTree(fs::path(Paths::GetEngineRoot()) / L"Shaders", out / L"Shaders", result.filesCopied, result.message)) return result;

	// 3. 에셋: 엔진 콘텐츠 위에 프로젝트 콘텐츠를 덮는다 (런타임의 해석 순서와 같다). 패키지 안에서는 둘이 한 폴더다.
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
			if (fs::exists(found, ec) && !CopyOne(found, outAssets / texture, result.filesCopied, result.message)) return result;
		}
		CopyAssetTree(L"Textures", outAssets, result.filesCopied, result.message);   // 이름만 있는 기본 텍스처(bumps_normal 등)를 위해 전부
	}

	// 4. engine.ini: [game] startScene / title 을 빌드 사본에 쓴다 (원본은 건드리지 않는다).
	{
		const fs::path ini = outAssets / L"Config" / L"engine.ini";
		std::ifstream in(ini, std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		in.close();
		text += "\r\n; --- Build Game (" + options.name + ") ---\r\n[game]\r\nstartScene = " + options.startScene + "\r\ntitle = " + options.name + "\r\n";
		fs::create_directories(ini.parent_path(), ec);
		std::ofstream outIni(ini, std::ios::binary | std::ios::trunc);
		outIni << text;   // 같은 키가 앞에 있어도 마지막 값이 이긴다 (Config 는 키 → 값 맵)
	}
	// 5. 프로젝트 파일 사본: 런타임이 exe 옆에서 찾아 프로젝트 루트 = exe 폴더로 잡는다 (engineRoot 없음 → 전부 exe 옆).
	{
		std::ofstream sherlock(out / (Wide(options.name) + L".sherlock"), std::ios::binary | std::ios::trunc);
		sherlock << "{\n  \"name\": \"" << options.name << "\",\n  \"startScene\": \"" << options.startScene << "\",\n  \"engineRoot\": \"\",\n  \"format\": 1\n}\n";
		++result.filesCopied;
	}

	result.ok = true;
	result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	result.message = "built " + options.name + " (" + std::to_string(result.filesCopied) + " files, " + std::to_string(static_cast<int>(result.seconds)) + " s)";
	Log::Info("게임 빌드: %s → %s (%d 파일, %.1f 초)", options.name.c_str(), Utf8(result.outputDir).c_str(), result.filesCopied, result.seconds);
	if (options.openFolder) ShellExecuteW(nullptr, L"explore", result.outputDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	return result;
}

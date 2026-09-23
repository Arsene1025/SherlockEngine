#include "pch.h"
#include "App/ProjectLauncher.h"
#include "App/ProjectGenerator.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <commdlg.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

namespace
{
	std::wstring SettingsPath()
	{
		wchar_t buffer[MAX_PATH] = {};
		ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\SherlockEngine", buffer, MAX_PATH);
		std::error_code ec;
		fs::create_directories(buffer, ec);
		return std::wstring(buffer) + L"\\editor.json";
	}
	std::wstring Wide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
		return wide;
	}
	nlohmann::json LoadSettings()
	{
		std::ifstream in(SettingsPath(), std::ios::binary);
		if (!in) return nlohmann::json::object();
		try { return nlohmann::json::parse(in); }
		catch (...) { return nlohmann::json::object(); }
	}
	void SaveSettings(const nlohmann::json& settings)
	{
		std::ofstream out(SettingsPath(), std::ios::binary | std::ios::trunc);
		if (out) out << settings.dump(2);
	}
}

std::vector<ProjectLauncher::Recent> ProjectLauncher::LoadRecent()
{
	std::vector<Recent> list;
	const nlohmann::json settings = LoadSettings();
	for (const auto& item : settings.value("recentProjects", nlohmann::json::array()))
	{
		Recent recent;
		recent.name = item.value("name", "");
		recent.path = Wide(item.value("path", ""));
		std::error_code ec;
		if (!recent.path.empty() && fs::exists(recent.path, ec)) list.push_back(recent);   // 지워진 프로젝트는 목록에서 뺀다
	}
	return list;
}

void ProjectLauncher::AddRecent(const std::string& name, const std::wstring& path)
{
	std::vector<Recent> list = LoadRecent();
	list.erase(std::remove_if(list.begin(), list.end(), [&](const Recent& r) { return _wcsicmp(r.path.c_str(), path.c_str()) == 0; }), list.end());
	list.insert(list.begin(), Recent{ name, path });
	if (list.size() > 10) list.resize(10);
	nlohmann::json settings = LoadSettings();
	nlohmann::json array = nlohmann::json::array();
	for (const Recent& r : list) array.push_back({ { "name", r.name }, { "path", Log::ToUtf8(r.path.c_str()) } });
	settings["recentProjects"] = array;
	SaveSettings(settings);
}

void ProjectLauncher::RemoveRecent(const std::wstring& path)
{
	std::vector<Recent> list = LoadRecent();
	list.erase(std::remove_if(list.begin(), list.end(), [&](const Recent& r) { return _wcsicmp(r.path.c_str(), path.c_str()) == 0; }), list.end());
	nlohmann::json settings = LoadSettings();
	nlohmann::json array = nlohmann::json::array();
	for (const Recent& r : list) array.push_back({ { "name", r.name }, { "path", Log::ToUtf8(r.path.c_str()) } });
	settings["recentProjects"] = array;
	SaveSettings(settings);
}

std::wstring ProjectLauncher::BrowseForProjectFile(void* ownerWindow)
{
	wchar_t file[MAX_PATH] = {};
	OPENFILENAMEW ofn = { sizeof(ofn) };
	ofn.hwndOwner = static_cast<HWND>(ownerWindow);
	ofn.lpstrFilter = L"Sherlock project (*.sherlock)\0*.sherlock\0All files\0*.*\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = L"Open Project";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&ofn)) return L"";
	return file;
}

std::wstring ProjectLauncher::BrowseForFolder(void* ownerWindow, const std::wstring& initial)
{
	// Vista+ 폴더 선택 대화상자 (IFileOpenDialog, FOS_PICKFOLDERS). COM 은 AppBase 가 열어 두었다.
	IFileOpenDialog* dialog = nullptr;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return L"";
	DWORD options = 0;
	dialog->GetOptions(&options);
	dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
	dialog->SetTitle(L"Choose the folder that will contain the new project");
	if (!initial.empty())
	{
		IShellItem* folder = nullptr;
		if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&folder))))
		{
			dialog->SetFolder(folder);
			folder->Release();
		}
	}
	std::wstring result;
	if (SUCCEEDED(dialog->Show(static_cast<HWND>(ownerWindow))))
	{
		IShellItem* item = nullptr;
		if (SUCCEEDED(dialog->GetResult(&item)))
		{
			PWSTR path = nullptr;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
			{
				result = path;
				CoTaskMemFree(path);
			}
			item->Release();
		}
	}
	dialog->Release();
	return result;
}

std::wstring ProjectLauncher::GetDefaultProjectsDir()
{
	const std::wstring repo = ProjectGenerator::GetEngineRepoDir();
	if (!repo.empty()) return repo + L"Projects\\";
	wchar_t documents[MAX_PATH] = {};
	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, documents))) return std::wstring(documents) + L"\\SherlockProjects\\";
	return L"";
}

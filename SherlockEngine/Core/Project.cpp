#include "pch.h"
#include "Core/Project.h"
#include "Core/Log.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cctype>

namespace fs = std::filesystem;

namespace
{
	std::wstring Wide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
		return wide;
	}
	std::wstring WithSlash(std::wstring dir)
	{
		for (wchar_t& c : dir) if (c == L'/') c = L'\\';
		if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
		return dir;
	}
}

bool Project::IsValidName(const std::string& name)
{
	if (name.empty() || name.size() > 48) return false;
	if (!std::isalpha(static_cast<unsigned char>(name[0]))) return false;   // vcxproj 이름·C++ 매크로에 쓰임
	for (char c : name) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
	return true;
}

std::wstring Project::FindProjectFile(const std::wstring& dir)
{
	std::error_code ec;
	for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec))
	{
		if (entry.is_regular_file(ec) && entry.path().extension() == kExtension) return entry.path().wstring();
	}
	return L"";
}

std::wstring Project::FindUpwards(const std::wstring& startDir, int maxLevels)
{
	fs::path dir = startDir;
	for (int level = 0; level <= maxLevels && !dir.empty(); ++level)
	{
		const std::wstring found = FindProjectFile(dir.wstring());
		if (!found.empty()) return found;
		if (dir == dir.parent_path()) break;
		dir = dir.parent_path();
	}
	return L"";
}

bool Project::Load(const std::wstring& pathOrDir)
{
	std::error_code ec;
	std::wstring file = pathOrDir;
	if (fs::is_directory(file, ec)) file = FindProjectFile(file);
	if (file.empty() || !fs::is_regular_file(file, ec))
	{
		Log::Error("프로젝트 파일이 없다: %s", Log::ToUtf8(pathOrDir.c_str()).c_str());
		return false;
	}
	std::ifstream in(file, std::ios::binary);
	nlohmann::json root;
	try { root = nlohmann::json::parse(in); }
	catch (const std::exception& e)
	{
		Log::Error("프로젝트 파일 파싱 실패 (%s): %s", Log::ToUtf8(file.c_str()).c_str(), e.what());
		return false;
	}
	// 상대 경로(--project=..\..\Projects\X)로 열어도 최근 목록·창 제목·생성기가 같은 프로젝트로 인식하도록 절대 경로로 정규화함
	const fs::path canonical = fs::weakly_canonical(fs::absolute(file, ec), ec);
	if (!canonical.empty()) file = canonical.wstring();
	m_root = WithSlash(fs::path(file).parent_path().wstring());
	m_name = root.value("name", Log::ToUtf8(fs::path(file).stem().wstring().c_str()));
	startScene = root.value("startScene", "");
	engineRoot = WithSlash(Wide(root.value("engineRoot", "")));
	if (!engineRoot.empty() && !fs::is_directory(engineRoot, ec)) engineRoot.clear();   // 다른 PC로 옮긴 프로젝트: exe 옆에서 찾음
	Log::Info("프로젝트: %s (%s, 시작 씬 %s)", m_name.c_str(), Log::ToUtf8(m_root.c_str()).c_str(), startScene.empty() ? "(없음)" : startScene.c_str());
	return true;
}

bool Project::Save() const
{
	if (!IsLoaded()) return false;
	nlohmann::json root;
	root["name"] = m_name;
	root["startScene"] = startScene;
	root["engineRoot"] = Log::ToUtf8(engineRoot.c_str());
	root["format"] = 1;
	std::ofstream out(GetFilePath(), std::ios::binary | std::ios::trunc);
	if (!out) return false;
	out << root.dump(2);
	return true;
}

bool Project::Create(const std::wstring& parentDir, const std::string& name, const std::wstring& engineRoot, Project& out, std::string& error)
{
	if (!IsValidName(name)) { error = "invalid project name (letters, digits, _; starts with a letter)"; return false; }
	std::error_code ec;
	const fs::path root = fs::path(WithSlash(parentDir)) / Wide(name);
	if (fs::exists(root, ec)) { error = "folder already exists: " + Log::ToUtf8(root.wstring().c_str()); return false; }
	for (const wchar_t* sub : { L"Assets\\Scenes", L"Assets\\Textures", L"Assets\\Models", L"Scripts", L"Build" })
	{
		fs::create_directories(root / sub, ec);
		if (ec) { error = "cannot create " + Log::ToUtf8((root / sub).wstring().c_str()); return false; }
	}
	out.m_name = name;
	out.m_root = WithSlash(root.wstring());
	out.startScene = "Main.json";
	out.engineRoot = WithSlash(engineRoot);
	if (!out.Save()) { error = "cannot write " + Log::ToUtf8(out.GetFilePath().c_str()); return false; }
	Log::Info("새 프로젝트: %s → %s", name.c_str(), Log::ToUtf8(out.m_root.c_str()).c_str());
	return true;
}

std::wstring Project::GetFilePath() const
{
	return m_root + Wide(m_name) + kExtension;
}

std::wstring Project::GetSolutionPath() const
{
	return m_root + Wide(m_name) + L".sln";
}

std::wstring Project::GetEditorProjectName() const
{
	return Wide(m_name) + L"Editor";
}

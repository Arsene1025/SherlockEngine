#include "pch.h"
#include "Core/Paths.h"
#include "Core/Log.h"

namespace
{
	std::wstring QueryExecutableDir()
	{
		wchar_t buffer[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
		{
			Log::Error("GetModuleFileNameW 실패. 작업 디렉터리를 대신 쓴다.");
			return L".\\";
		}

		std::wstring path(buffer, length);
		const size_t lastSlash = path.find_last_of(L"\\/");
		if (lastSlash == std::wstring::npos)
		{
			return L".\\";
		}
		return path.substr(0, lastSlash + 1);
	}

	// "..\.." 같은 상대 요소를 정리한 절대 경로. 로그에 찍을 때 읽기 좋음.
	std::wstring Canonicalize(const std::wstring& path)
	{
		wchar_t buffer[MAX_PATH] = {};
		const DWORD length = GetFullPathNameW(path.c_str(), MAX_PATH, buffer, nullptr);
		if (length == 0 || length >= MAX_PATH)
		{
			return path;
		}
		return std::wstring(buffer, length);
	}

	bool FileExists(const std::wstring& path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	bool DirectoryExists(const std::wstring& path)
	{
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	std::wstring WithSlash(std::wstring dir)
	{
		if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
		return dir;
	}

	// 11-E단계: 루트 상태. 엔진 루트는 처음 조회할 때 마커로 정하고, 프로젝트 파일이 더 정확한 값을 주면 덮어씀.
	std::wstring s_engineRoot;
	bool s_engineIsSourceTree = false;
	std::wstring s_projectRoot;

	void EnsureEngineRoot()
	{
		if (!s_engineRoot.empty()) return;
		// 엔진 저장소 빌드: exe 는 <repo>\x64\<Config>\ 에 있으므로 두 단계 위가 저장소 루트임.
		const std::wstring marker = Canonicalize(Paths::GetExecutableDir() + L"..\\..\\SherlockEngine\\Assets\\Config\\engine.ini");
		if (FileExists(marker))
		{
			s_engineRoot = Canonicalize(Paths::GetExecutableDir() + L"..\\..\\SherlockEngine") + L"\\";
			s_engineIsSourceTree = true;
		}
		else
		{
			s_engineRoot = Paths::GetExecutableDir();   // 배포: 전부 exe 옆
			s_engineIsSourceTree = false;
		}
	}

	std::wstring PrefixRelative(const std::wstring& root, const std::wstring& full)
	{
		if (root.empty() || full.size() < root.size() || _wcsnicmp(full.c_str(), root.c_str(), root.size()) != 0) return L"";
		std::wstring relative = full.substr(root.size());
		for (wchar_t& c : relative) if (c == L'/') c = L'\\';
		return relative;
	}
}

const std::wstring& Paths::GetExecutableDir()
{
	static const std::wstring dir = QueryExecutableDir();
	return dir;
}

void Paths::SetEngineRoot(const std::wstring& dir)
{
	if (dir.empty()) return;
	const std::wstring canonical = WithSlash(Canonicalize(dir));
	if (!DirectoryExists(canonical + L"Assets") || !DirectoryExists(canonical + L"Shaders")) return;   // 다른 PC의 경로 등은 무시
	s_engineRoot = canonical;
	s_engineIsSourceTree = true;
}

const std::wstring& Paths::GetEngineRoot()
{
	EnsureEngineRoot();
	return s_engineRoot;
}

bool Paths::HasEngineSourceTree()
{
	EnsureEngineRoot();
	return s_engineIsSourceTree;
}

void Paths::SetProjectRoot(const std::wstring& dir)
{
	s_projectRoot = dir.empty() ? L"" : WithSlash(Canonicalize(dir));
}

const std::wstring& Paths::GetProjectRoot()
{
	return s_projectRoot;
}

bool Paths::HasProject()
{
	return !s_projectRoot.empty();
}

std::wstring Paths::GetEngineAssetRoot()
{
	const std::wstring root = GetEngineRoot() + L"Assets\\";
	return DirectoryExists(root) ? root : GetExecutableDir() + L"Assets\\";
}

std::wstring Paths::GetAssetRoot()
{
	if (HasProject()) return s_projectRoot + L"Assets\\";
	return GetEngineAssetRoot();
}

std::wstring Paths::GetShaderPath(const wchar_t* fileName)
{
	const std::wstring primary = GetExecutableDir() + L"Shaders\\" + fileName;
	if (FileExists(primary))
	{
		return primary;
	}

	// 빌드 산출물이 없을 때(예: 셰이더만 고치고 빌드하지 않은 채 실행, 또는 프로젝트 에디터의 Binaries\ 에서 실행)
	// 엔진 루트의 원본으로 폴백함.
	const std::wstring fallback = GetEngineRoot() + L"Shaders\\" + fileName;
	if (FileExists(fallback))
	{
		static bool warned = false;
		if (!warned)
		{
			warned = true;
			Log::Warn("exe 옆 Shaders 폴더에 셰이더가 없어 엔진 루트로 폴백함: %s", Log::ToUtf8(fallback.c_str()).c_str());
		}
		return fallback;
	}

	Log::Error("셰이더 파일을 찾지 못함: %s", Log::ToUtf8(primary.c_str()).c_str());
	return primary;
}

std::wstring Paths::GetShaderBinaryPath(const wchar_t* fileName)
{
	return GetExecutableDir() + L"Shaders\\" + fileName;
}

std::wstring Paths::GetShaderSourcePath(const wchar_t* fileName)
{
	const std::wstring source = GetEngineRoot() + L"Shaders\\" + fileName;
	if (FileExists(source))
	{
		return source;
	}
	return GetExecutableDir() + L"Shaders\\" + fileName;
}

std::wstring Paths::GetAssetPath(const wchar_t* relative)
{
	if (HasProject())
	{
		const std::wstring project = s_projectRoot + L"Assets\\" + relative;
		if (FileExists(project)) return project;
	}
	const std::wstring engine = GetEngineAssetRoot() + relative;
	if (FileExists(engine)) return engine;
	const std::wstring exe = GetExecutableDir() + L"Assets\\" + relative;
	if (FileExists(exe)) return exe;
	return HasProject() ? s_projectRoot + L"Assets\\" + relative : engine;
}

std::wstring Paths::GetSceneDir()
{
	const std::wstring dir = GetAssetRoot() + L"Scenes\\";
	CreateDirectoryW(dir.c_str(), nullptr);
	return dir;
}

std::wstring Paths::ToAssetRelative(const std::wstring& absolute)
{
	const std::wstring full = Canonicalize(absolute);
	if (HasProject())
	{
		const std::wstring relative = PrefixRelative(s_projectRoot + L"Assets\\", full);
		if (!relative.empty()) return relative;
	}
	const std::wstring relative = PrefixRelative(GetEngineAssetRoot(), full);
	if (!relative.empty()) return relative;
	return PrefixRelative(GetExecutableDir() + L"Assets\\", full);
}

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

	// "..\.." 같은 상대 요소를 정리한 절대 경로. 로그에 찍을 때 읽기 좋다.
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
}

const std::wstring& Paths::GetExecutableDir()
{
	static const std::wstring dir = QueryExecutableDir();
	return dir;
}

std::wstring Paths::GetShaderPath(const wchar_t* fileName)
{
	const std::wstring primary = GetExecutableDir() + L"Shaders\\" + fileName;
	if (FileExists(primary))
	{
		return primary;
	}

	// 빌드 산출물이 없을 때(예: 셰이더만 고치고 빌드하지 않은 채 실행)
	// 소스 트리의 원본으로 폴백한다. exe는 <repo>\x64\<Config>\ 에 있으므로
	// 두 단계 위가 저장소 루트다.
	const std::wstring fallback = Canonicalize(GetExecutableDir() + L"..\\..\\SherlockEngine\\Shaders\\" + fileName);
	if (FileExists(fallback))
	{
		static bool warned = false;
		if (!warned)
		{
			warned = true;
			Log::Warn("exe 옆 Shaders 폴더에 셰이더가 없어 소스 트리로 폴백함: %s", Log::ToUtf8(fallback.c_str()).c_str());
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

std::wstring Paths::GetAssetPath(const wchar_t* relative)
{
	const std::wstring primary = GetExecutableDir() + L"Assets\\" + relative;
	if (FileExists(primary))
	{
		return primary;
	}
	const std::wstring fallback = Canonicalize(GetExecutableDir() + L"..\\..\\SherlockEngine\\Assets\\" + relative);
	if (FileExists(fallback))
	{
		return fallback;
	}
	return primary;
}

std::wstring Paths::GetShaderSourcePath(const wchar_t* fileName)
{
	const std::wstring source = Canonicalize(GetExecutableDir() + L"..\\..\\SherlockEngine\\Shaders\\" + fileName);
	if (FileExists(source))
	{
		return source;
	}
	return GetExecutableDir() + L"Shaders\\" + fileName;
}

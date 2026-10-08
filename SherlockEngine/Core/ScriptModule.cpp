#include "pch.h"
#include "Core/ScriptModule.h"
#include "Core/Project.h"
#include "Core/Log.h"
#include "Scene/Behaviour.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace
{
	uint64_t GetWriteTime(const std::wstring& path)
	{
		WIN32_FILE_ATTRIBUTE_DATA data = {};
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
		return (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
	}

	uint64_t Now()
	{
		FILETIME now = {};
		GetSystemTimeAsFileTime(&now);
		return (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
	}

	// 복사본 폴더의 지난 복사본을 지움. 다른 에디터가 올려 둔 것(잠김)은 지워지지 않고 남음 — 그래도 됨.
	void CleanOldCopies(const fs::path& dir)
	{
		std::error_code ec;
		for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec))
		{
			const std::wstring ext = entry.path().extension().wstring();
			if (ext == L".dll") fs::remove(entry.path(), ec);
		}
	}
}

ScriptModule::~ScriptModule()
{
	// 일부러 내리지 않음. 앱이 끝날 때는 엔진이 씬을 비우는 시점(Engine::Shutdown)이 이 소멸자보다 늦을 수 있어,
	// 여기서 FreeLibrary 하면 스크립트 컴포넌트의 소멸자가 사라진 코드를 부르게 됨. 프로세스가 끝나면 OS 가 정리함.
}

const wchar_t* ScriptModule::GetConfiguration()
{
#if defined(_DEBUG)
	return L"Debug";
#else
	return L"Release";
#endif
}

std::wstring ScriptModule::GetDllPath(const Project& project)
{
	return project.GetBinariesDir(GetConfiguration()) + std::wstring(project.GetName().begin(), project.GetName().end()) + L"Scripts.dll";
}

size_t ScriptModule::CountScriptSources(const Project& project)
{
	size_t count = 0;
	std::error_code ec;
	for (const fs::directory_entry& entry : fs::recursive_directory_iterator(project.GetScriptsDir(), ec))
	{
		if (entry.is_regular_file(ec) && entry.path().extension() == L".cpp") ++count;
	}
	return count;
}

bool ScriptModule::Load(const std::wstring& dllPath, bool copyBeforeLoad)
{
	Unload();
	m_path = dllPath;
	m_lastError.clear();
	if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		m_lastError = "not built yet: " + Log::ToUtf8(dllPath.c_str());
		return false;
	}

	// 복사본: <DLL 폴더>\Loaded\<이름>_<프로세스>_<번호>.dll. 원본은 잠기지 않으므로 에디터를 켠 채로 다시 빌드할 수 있음.
	// PDB 는 복사하지 않음 — DLL 안에 원본 PDB 의 절대 경로가 들어 있어 디버거는 원본 옆의 PDB 를 찾음.
	const fs::path original(dllPath);
	fs::path copy = original;
	std::error_code ec;
	const uint64_t writeTime = GetWriteTime(dllPath);
	m_triedWriteTime = writeTime;   // 실패해도 기록 — 같은 파일을 1초마다 다시 시도하지 않음
	if (copyBeforeLoad)
	{
		const fs::path copyDir = original.parent_path() / L"Loaded";
		fs::create_directories(copyDir, ec);
		if (m_reloads == 0) CleanOldCopies(copyDir);
		const std::wstring copyName = original.stem().wstring() + L"_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(m_reloads) + L".dll";
		copy = copyDir / copyName;
		if (!fs::copy_file(original, copy, fs::copy_options::overwrite_existing, ec))
		{
			m_lastError = "cannot copy the DLL (" + ec.message() + ")";
			return false;
		}
	}

	// 올리기 전의 이름 목록과 비교해 이 DLL 이 등록한 이름을 알아냄 (정적 초기화가 LoadLibrary 안에서 등록함).
	const std::vector<std::string> before = BehaviourRegistry::GetTypeNames();
	HMODULE module = LoadLibraryW(copy.c_str());
	if (module == nullptr)
	{
		m_lastError = "LoadLibrary failed (" + Log::HrToString(HRESULT_FROM_WIN32(::GetLastError())) + ")";
		if (copyBeforeLoad) fs::remove(copy, ec);
		return false;
	}
	m_module = module;
	m_loadedCopy = copyBeforeLoad ? copy.wstring() : std::wstring();
	m_loadedWriteTime = writeTime;
	++m_reloads;
	for (const std::string& name : BehaviourRegistry::GetTypeNames())
	{
		if (std::find(before.begin(), before.end(), name) == before.end()) m_typeNames.push_back(name);
	}
	std::string list;
	for (const std::string& name : m_typeNames) list += (list.empty() ? "" : ", ") + name;
	Log::Info("스크립트 DLL 로드: %s (%zu개: %s)", Log::ToUtf8(dllPath.c_str()).c_str(), m_typeNames.size(), list.c_str());
	return true;
}

void ScriptModule::Unload()
{
	if (m_module == nullptr) return;
	for (const std::string& name : m_typeNames) BehaviourRegistry::Unregister(name);
	FreeLibrary(static_cast<HMODULE>(m_module));
	m_module = nullptr;
	std::error_code ec;
	if (!m_loadedCopy.empty()) std::filesystem::remove(m_loadedCopy, ec);   // FreeLibrary 뒤에는 잠기지 않음
	Log::Info("스크립트 DLL 내림: %s (컴포넌트 %zu개 등록 해제)", Log::ToUtf8(m_path.c_str()).c_str(), m_typeNames.size());
	m_typeNames.clear();
	m_loadedCopy.clear();
	m_loadedWriteTime = 0;
}

bool ScriptModule::HasNewerBuild() const
{
	if (m_path.empty()) return false;
	const uint64_t current = GetWriteTime(m_path);
	if (current == 0 || current == m_triedWriteTime) return false;
	// 링커가 아직 쓰는 중일 수 있음 — 마지막 수정에서 1초(1e7 × 100ns) 이상 지난 뒤에만 새 빌드로 봄.
	return Now() - current > 10'000'000ull;
}

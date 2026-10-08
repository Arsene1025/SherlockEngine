#pragma once
#include <cstdint>
#include <string>
#include <vector>

class Project;

// 2026-10-08: 프로젝트 스크립트 DLL (B안). 언리얼의 게임 모듈 DLL 에 해당함.
//
// 프로젝트의 Scripts\**\*.cpp 는 <프로젝트>\Binaries\<구성>\<이름>Scripts.dll 로 빌드됨 (ProjectGenerator 가 만든 <이름>Scripts.vcxproj).
// 에디터와 게임은 프로젝트를 열 때 이 DLL 을 LoadLibrary 로 올림. 올리는 순간 DLL 의 정적 초기화가 SHERLOCK_SCRIPT 등록을 실행해
// 엔진 DLL 안의 BehaviourRegistry 에 스크립트가 들어감 — 별도의 "등록 함수" 호출이 없음.
//
// 핫리로드: 원본 DLL 을 그대로 올리면 파일이 잠겨 다시 빌드할 수 없으므로, <DLL 폴더>\Loaded\ 에 복사본(이름에 프로세스·번호)을 만들어 그것을 올림.
// 원본은 언제든 다시 빌드할 수 있고, HasNewerBuild() 가 원본의 수정 시각이 올린 것보다 새로우면 참이 됨.
// 내리기 전에 반드시 이 DLL 의 클래스로 만든 컴포넌트를 모두 파괴해야 함 (씬을 비움). 생성 함수와 vtable 이 DLL 코드를 가리키기 때문.
// 순서는 호출자(EditorApp::ReloadScripts)가 지킴: 씬 스냅샷 → 씬 비움 → Unload → Load → 스냅샷 복원.
//
// 빌드 구성: 엔진이 Debug 면 Binaries\Debug, Release 면 Binaries\Release 의 DLL 을 씀. Debug 와 Release 는 STL 배치가 달라 섞이면 안 됨.
class ScriptModule
{
public:
	ScriptModule() = default;
	~ScriptModule();   // 내리지 않음 (아래 주석)
	ScriptModule(const ScriptModule&) = delete;
	ScriptModule& operator=(const ScriptModule&) = delete;

	// 이 빌드 구성에서 프로젝트의 스크립트 DLL 경로 (<root>Binaries\<구성>\<이름>Scripts.dll). 파일이 없어도 경로를 돌려줌.
	static std::wstring GetDllPath(const Project& project);
	static const wchar_t* GetConfiguration();   // L"Debug" / L"Release" (엔진 빌드 구성)
	static size_t CountScriptSources(const Project& project);   // Scripts\ 아래 .cpp 개수 (재귀)

	// 원본 DLL 경로로 올림. 이미 올린 것이 있으면 먼저 Unload 함 (호출자가 씬을 비운 뒤여야 함). 파일이 없거나 LoadLibrary 가 실패하면 false.
	// copyBeforeLoad: 복사본을 올려 원본을 잠그지 않음 (에디터의 핫리로드). 게임은 원본을 그대로 올림.
	bool Load(const std::wstring& dllPath, bool copyBeforeLoad = true);
	// 이 DLL 이 등록한 컴포넌트 이름을 레지스트리에서 지우고 FreeLibrary. 그 클래스의 객체가 남아 있으면 안 됨.
	void Unload();

	bool IsLoaded() const { return m_module != nullptr; }
	const std::wstring& GetPath() const { return m_path; }                 // 원본 경로 (Load 실패해도 마지막으로 시도한 경로)
	const std::vector<std::string>& GetTypeNames() const { return m_typeNames; }   // 이 DLL 이 등록한 컴포넌트
	const std::string& GetLastError() const { return m_lastError; }
	bool HasNewerBuild() const;   // 원본이 올린 시점보다 새로 빌드됐는지. 링커가 쓰는 중일 수 있으므로 1초 이상 지난 파일만 참
	uint32_t GetReloadCount() const { return m_reloads; }

private:
	void* m_module = nullptr;      // HMODULE (windows.h 를 헤더에 끌어오지 않으려고 void*)
	std::wstring m_path;
	std::wstring m_loadedCopy;     // 실제로 올린 복사본
	uint64_t m_loadedWriteTime = 0;
	uint64_t m_triedWriteTime = 0; // 마지막으로 올리려고 한 원본의 수정 시각 (실패 포함). HasNewerBuild 의 기준
	std::vector<std::string> m_typeNames;
	std::string m_lastError;
	uint32_t m_reloads = 0;
};

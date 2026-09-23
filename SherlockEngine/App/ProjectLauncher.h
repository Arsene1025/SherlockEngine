#pragma once
#include <string>
#include <vector>

// 11-E단계: 프로젝트 런처의 데이터 쪽 — 최근 프로젝트 목록(%LOCALAPPDATA%\SherlockEngine\editor.json)과 네이티브 파일/폴더 대화상자.
// 그리기(ImGui 창)는 Editor::DrawLauncher 가 한다.
namespace ProjectLauncher
{
	struct Recent
	{
		std::string name;
		std::wstring path;    // .sherlock 파일
	};
	std::vector<Recent> LoadRecent();
	void AddRecent(const std::string& name, const std::wstring& path);   // 맨 앞으로, 최대 10개
	void RemoveRecent(const std::wstring& path);

	std::wstring BrowseForProjectFile(void* ownerWindow);   // *.sherlock 열기 대화상자. 취소면 빈 문자열
	std::wstring BrowseForFolder(void* ownerWindow, const std::wstring& initial);   // 새 프로젝트를 만들 부모 폴더
	std::wstring GetDefaultProjectsDir();                   // <엔진 저장소>\Projects\ (소스 트리 없으면 문서 폴더)
}

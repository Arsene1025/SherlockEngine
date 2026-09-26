#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <DirectXMath.h>
#include "RHI/Handle.h"

namespace RHI { class Device; }
struct ImDrawList;
struct ImVec2;

// 11-B단계: 콘텐츠 브라우저. Assets\ 폴더를 폴더 트리 + 타일 그리드로 보여주고, 타일을 드래그해
// Scene 뷰에 놓으면 에디터가 그 에셋을 배치함 (모델 → 커서 아래에 배치, 씬 → 로드, 텍스처 → 맞은 오브젝트의 재질에 적용).
//
// 11-E단계: 루트가 둘임 — 프로젝트 Assets\ (Project) 와 엔진 Assets\ (Engine, 언리얼의 Engine Content). 드래그 페이로드의
// 상대 경로에는 루트를 적지 않음: Paths::GetAssetPath 가 프로젝트 → 엔진 순으로 찾으므로 어느 트리의 것이든 같은 이름으로 해석됨.
//
// 소유하지 않음: 파일 시스템을 읽기만 하고, 씬 변경은 Actions 콜백으로 Editor 에 넘김 (Editor 가 다시 앱에 넘김).
// 유일하게 소유하는 GPU 자원은 텍스처 썸네일임. 렌더러의 텍스처 캐시와는 완전히 분리함 —
// Renderer::InvalidateScene(…, true) 가 씬 전환마다 텍스처를 지우기 때문임.
enum class AssetType : uint8_t { Folder, Model, Texture, Scene, Other };

// 드래그 페이로드. ImGui 가 memcpy 로 복사하므로 POD 고정 크기여야 함 (≈524B, ImGui 는 16B 를 넘으면 힙 버퍼에 복사함).
struct AssetDragPayload
{
	AssetType type = AssetType::Other;
	wchar_t relativePath[260] = {};   // Assets 기준 상대 경로, 구분자는 백슬래시. 받는 쪽은 마지막 원소에 종료 문자를 넣어 방어함.
};
constexpr const char* kAssetPayloadType = "SHERLOCK_ASSET";

class ContentBrowser
{
public:
	struct Actions
	{
		std::function<void(const std::wstring& relativePath)> openScene;                                        // 씬 더블클릭·컨텍스트 메뉴
		std::function<void(const std::wstring& relativePath, const DirectX::XMFLOAT3& position)> placeModel;    // 모델 더블클릭 → 원점
	};

	ContentBrowser();
	~ContentBrowser();   // 썸네일 텍스처를 마지막 Draw 의 Device 로 파괴함 (TestApp 이 Engine::Shutdown 보다 먼저 소멸)

	// "Content Browser" 창의 내용. ImGui::Begin/End 는 호출자(Editor) 의 몫.
	void Draw(RHI::Device& device, const Actions& actions);

	void Refresh() { m_dirty = true; m_rootsDirty = true; }   // 루트(프로젝트)가 바뀌었을 때도 이것을 호출
	void SetDirectory(const std::wstring& relativeDir, int root = 0);   // L"" = 루트. root 0 = 프로젝트, 1 = 엔진
	const std::wstring& GetDirectory() const { return m_currentDir; }
	size_t GetEntryCount() const { return m_entries.size(); }
	size_t GetThumbnailCount() const { return m_thumbnails.size(); }
	bool IsDragCancelled() const { return m_dragCancelled; }   // 드래그 중 Esc 를 누르면 참. 드롭 타깃은 이것이 참이면 받지 않음

	static AssetType Classify(const std::wstring& relativePath, bool isDirectory);
	static const char* TypeName(AssetType type);
	// 재질 텍스처 이름 규칙 (Material.h): Assets\Textures\x.png → "x.png", 그 밖의 파일 → "asset:<상대 경로>"
	static std::string ToMaterialTextureName(const std::wstring& relativePath);
	// 타입 아이콘 (ImDrawList 만 씀). 드래그 미리보기·고스트 마커도 같은 그림을 씀.
	static void DrawTypeIcon(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, AssetType type, float alpha = 1.0f);

private:
	struct Root
	{
		std::string label;     // "Project" / "Engine"
		std::wstring dir;      // 절대 경로, 끝 백슬래시
	};
	struct Entry
	{
		std::wstring name;
		std::string nameUtf8;         // ImGui 라벨. 변환은 열거할 때 한 번만
		std::wstring relativePath;    // 루트 기준
		std::wstring absolutePath;
		AssetType type = AssetType::Other;
		bool isDirectory = false;
		bool hidden = false;          // .bin/.ini/.log — "Hidden" 토글을 켜야 회색으로 보임
		uintmax_t size = 0;
	};
	struct TreeNode
	{
		std::wstring name;
		std::string nameUtf8;
		std::wstring relativePath;
		int root = 0;
		std::vector<TreeNode> children;
	};
	struct Thumbnail
	{
		TextureHandle handle;
		bool failed = false;
		uint64_t lastUsedFrame = 0;
	};

	void RefreshRoots();
	void EnumerateDirectory();
	void BuildTree(TreeNode& node, const std::wstring& absoluteDir, int depth);
	void DrawToolbar();
	void DrawBreadcrumb();
	void DrawFolderTree(const TreeNode& node);
	void DrawTileGrid(const Actions& actions);
	void DrawTile(int entryIndex, const ImVec2& tileSize, const Actions& actions);
	void BeginDragSource(const Entry& entry);
	void DrawContextMenu(const Entry& entry, const Actions& actions);
	void OnDoubleClick(const Entry& entry, const Actions& actions);
	const Root& CurrentRoot() const { return m_roots[m_currentRoot]; }

	uint64_t RequestThumbnail(const Entry& entry);      // ImTextureID 또는 0 (아직 로드 전이거나 실패). 처음 요청되면 로드 큐에 넣음
	void PumpThumbnailLoads(uint32_t budget);           // 프레임당 budget 장만 디코딩 (히치 방지)
	void EvictThumbnails();                             // 상한을 넘으면 오래 안 쓴 것부터 파괴
	void ClearThumbnails();

	std::vector<Root> m_roots;                           // [0] 프로젝트 (없으면 엔진만), [1] 엔진
	bool m_rootsDirty = true;
	int m_currentRoot = 0;
	std::wstring m_currentDir;                           // 루트 기준 상대. L"" = 루트
	std::vector<Entry> m_entries;
	std::vector<TreeNode> m_trees;                       // 루트마다 하나
	bool m_dirty = true;
	bool m_expandTreeToCurrent = false;                  // 그리드에서 폴더에 들어갔을 때 트리의 조상을 펼침
	double m_lastPollTime = 0.0;
	int64_t m_dirStamp = 0;                              // last_write_time. 1초마다 비교해 바뀌면 다시 읽음
	int m_selected = -1;
	char m_search[128] = {};
	float m_tileSize = 96.0f;
	bool m_showHidden = false;
	bool m_dragCancelled = false;

	RHI::Device* m_device = nullptr;
	std::unordered_map<std::wstring, Thumbnail> m_thumbnails;   // 키 = 절대 경로
	std::deque<std::wstring> m_pending;
	uint64_t m_frame = 0;
};

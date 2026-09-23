#include "pch.h"
#include "App/ContentBrowser.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include "Graphics/TextureLoader.h"
#include "RHI/Device.h"
#include <imgui.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

namespace
{
	constexpr uint32_t kThumbnailSize = 128;          // 긴 변
	constexpr size_t kMaxThumbnails = 256;            // 128² RGBA = 64KB → 최악 16MB. D3D12 shader-visible SRV 힙(4096) 도 지킨다
	constexpr size_t kEvictTo = 192;
	constexpr uint32_t kThumbnailLoadsPerFrame = 1;   // 2048² JPEG 디코드+축소 ≈ 10~30ms. 한 장씩이면 히치가 한 프레임에 그친다
	constexpr int kMaxTreeDepth = 6;

	std::wstring ToLower(std::wstring s)
	{
		std::transform(s.begin(), s.end(), s.begin(), ::towlower);
		return s;
	}

	std::wstring Extension(const std::wstring& path)
	{
		const size_t dot = path.find_last_of(L'.');
		const size_t slash = path.find_last_of(L"\\/");
		if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return L"";
		return ToLower(path.substr(dot));
	}

	std::wstring FileName(const std::wstring& path)
	{
		const size_t slash = path.find_last_of(L"\\/");
		return slash == std::wstring::npos ? path : path.substr(slash + 1);
	}

	bool StartsWithNoCase(const std::wstring& s, const std::wstring& prefix)
	{
		return s.size() >= prefix.size() && _wcsnicmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
	}

	bool ContainsNoCase(const std::string& haystack, const std::string& needle)
	{
		if (needle.empty()) return true;
		auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
			[](char a, char b) { return ::tolower(static_cast<unsigned char>(a)) == ::tolower(static_cast<unsigned char>(b)); });
		return it != haystack.end();
	}

	// UTF-8 경계를 지켜 maxWidth 안에 들어가게 자르고 "…" 을 붙인다.
	std::string TruncateLabel(const std::string& text, float maxWidth)
	{
		if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
		const char* ellipsis = "...";
		const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
		size_t length = text.size();
		while (length > 0)
		{
			--length;
			while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) --length;   // 연속 바이트는 건너뛴다
			if (ImGui::CalcTextSize(text.c_str(), text.c_str() + length).x + ellipsisWidth <= maxWidth) break;
		}
		return text.substr(0, length) + ellipsis;
	}

	std::string SizeText(uintmax_t bytes)
	{
		char buffer[32];
		if (bytes >= 1024ull * 1024ull) snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / (1024.0 * 1024.0));
		else if (bytes >= 1024ull) snprintf(buffer, sizeof(buffer), "%.0f KB", bytes / 1024.0);
		else snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
		return buffer;
	}

	int64_t DirectoryStamp(const fs::path& dir)
	{
		std::error_code ec;
		const fs::file_time_type t = fs::last_write_time(dir, ec);
		return ec ? 0 : static_cast<int64_t>(t.time_since_epoch().count());
	}
}

// ------------------------------------------------------------------ 정적 유틸

AssetType ContentBrowser::Classify(const std::wstring& relativePath, bool isDirectory)
{
	if (isDirectory) return AssetType::Folder;
	const std::wstring ext = Extension(relativePath);
	if (ext == L".gltf" || ext == L".glb") return AssetType::Model;
	if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" || ext == L".dds" || ext == L".tga") return AssetType::Texture;
	if (ext == L".json" && StartsWithNoCase(relativePath, L"Scenes\\")) return AssetType::Scene;
	return AssetType::Other;
}

const char* ContentBrowser::TypeName(AssetType type)
{
	switch (type)
	{
	case AssetType::Folder: return "Folder";
	case AssetType::Model: return "Model (glTF)";
	case AssetType::Texture: return "Texture";
	case AssetType::Scene: return "Scene";
	default: return "File";
	}
}

std::string ContentBrowser::ToMaterialTextureName(const std::wstring& relativePath)
{
	// Textures\ 바로 아래 파일은 기존 규칙(파일명만) — 예전 씬 파일과 데모 씬의 이름과 같은 형태를 유지한다.
	const std::wstring prefix = L"Textures\\";
	if (StartsWithNoCase(relativePath, prefix) && relativePath.find(L'\\', prefix.size()) == std::wstring::npos)
	{
		return Log::ToUtf8(relativePath.substr(prefix.size()).c_str());
	}
	return "asset:" + Log::ToUtf8(relativePath.c_str());
}

void ContentBrowser::DrawTypeIcon(ImDrawList* dl, const ImVec2& min, const ImVec2& max, AssetType type, float alpha)
{
	const float w = max.x - min.x;
	const float h = max.y - min.y;
	const float s = (std::min)(w, h);
	const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
	const auto col = [alpha](int r, int g, int b) { return IM_COL32(r, g, b, static_cast<int>(255 * alpha)); };

	switch (type)
	{
	case AssetType::Folder:
	{
		// 탭 + 몸통
		const ImVec2 a(c.x - s * 0.42f, c.y - s * 0.30f);
		const ImVec2 b(c.x + s * 0.42f, c.y + s * 0.32f);
		dl->AddRectFilled(ImVec2(a.x, a.y - s * 0.10f), ImVec2(a.x + s * 0.34f, a.y + s * 0.08f), col(214, 160, 52), 3.0f);
		dl->AddRectFilled(a, b, col(240, 190, 70), 4.0f);
		dl->AddRectFilled(ImVec2(a.x, a.y + s * 0.10f), b, col(250, 205, 92), 4.0f);
		break;
	}
	case AssetType::Model:
	{
		// 등각 정육면체: 윗면·왼쪽면·오른쪽면 세 가지 밝기
		const float r = s * 0.40f;
		const ImVec2 top(c.x, c.y - r);
		const ImVec2 left(c.x - r * 0.87f, c.y - r * 0.5f);
		const ImVec2 right(c.x + r * 0.87f, c.y - r * 0.5f);
		const ImVec2 mid(c.x, c.y);
		const ImVec2 bottomLeft(c.x - r * 0.87f, c.y + r * 0.5f);
		const ImVec2 bottomRight(c.x + r * 0.87f, c.y + r * 0.5f);
		const ImVec2 bottom(c.x, c.y + r);
		const ImVec2 topFace[4] = { top, right, mid, left };
		const ImVec2 leftFace[4] = { left, mid, bottom, bottomLeft };
		const ImVec2 rightFace[4] = { mid, right, bottomRight, bottom };
		dl->AddConvexPolyFilled(topFace, 4, col(120, 150, 240));
		dl->AddConvexPolyFilled(leftFace, 4, col(58, 82, 170));
		dl->AddConvexPolyFilled(rightFace, 4, col(86, 116, 210));
		dl->AddPolyline(topFace, 4, col(30, 40, 90), ImDrawFlags_Closed, 1.0f);
		dl->AddLine(mid, bottom, col(30, 40, 90), 1.0f);
		break;
	}
	case AssetType::Scene:
	case AssetType::Other:
	{
		// 문서: 접힌 모서리 + 줄. 씬은 파란 줄 + 작은 큐브 점.
		const ImVec2 a(c.x - s * 0.30f, c.y - s * 0.40f);
		const ImVec2 b(c.x + s * 0.30f, c.y + s * 0.40f);
		const float fold = s * 0.16f;
		const ImU32 paper = type == AssetType::Scene ? col(236, 240, 255) : col(232, 232, 236);
		const ImU32 ink = type == AssetType::Scene ? col(70, 90, 200) : col(150, 150, 160);
		const ImVec2 outline[5] = { a, ImVec2(b.x - fold, a.y), ImVec2(b.x, a.y + fold), b, ImVec2(a.x, b.y) };
		dl->AddConvexPolyFilled(outline, 5, paper);
		dl->AddPolyline(outline, 5, ink, ImDrawFlags_Closed, 1.0f);
		dl->AddLine(ImVec2(b.x - fold, a.y), ImVec2(b.x - fold, a.y + fold), ink, 1.0f);
		dl->AddLine(ImVec2(b.x - fold, a.y + fold), ImVec2(b.x, a.y + fold), ink, 1.0f);
		for (int i = 0; i < 3; ++i)
		{
			const float y = a.y + s * (0.32f + 0.14f * i);
			dl->AddLine(ImVec2(a.x + s * 0.08f, y), ImVec2(b.x - s * 0.08f, y), ink, 2.0f);
		}
		if (type == AssetType::Scene) dl->AddRectFilled(ImVec2(a.x + s * 0.08f, b.y - s * 0.20f), ImVec2(a.x + s * 0.22f, b.y - s * 0.06f), col(120, 150, 240));
		break;
	}
	case AssetType::Texture:
	{
		// 썸네일이 아직 없을 때의 체커 자리표시자
		const float half = s * 0.36f;
		const ImU32 light = col(200, 200, 205);
		const ImU32 dark = col(140, 140, 150);
		dl->AddRectFilled(ImVec2(c.x - half, c.y - half), ImVec2(c.x, c.y), light);
		dl->AddRectFilled(ImVec2(c.x, c.y - half), ImVec2(c.x + half, c.y), dark);
		dl->AddRectFilled(ImVec2(c.x - half, c.y), ImVec2(c.x, c.y + half), dark);
		dl->AddRectFilled(ImVec2(c.x, c.y), ImVec2(c.x + half, c.y + half), light);
		break;
	}
	}
}

// ------------------------------------------------------------------ 수명

ContentBrowser::ContentBrowser()
{
}

ContentBrowser::~ContentBrowser()
{
	ClearThumbnails();
}

// ------------------------------------------------------------------ 열거

void ContentBrowser::SetDirectory(const std::wstring& relativeDir, int root)
{
	if (m_rootsDirty) RefreshRoots();
	if (root < 0 || root >= static_cast<int>(m_roots.size())) root = 0;
	m_currentRoot = root;
	std::wstring dir = relativeDir;
	for (wchar_t& c : dir) if (c == L'/') c = L'\\';
	while (!dir.empty() && dir.back() == L'\\') dir.pop_back();
	m_currentDir = dir;
	m_dirty = true;
	m_expandTreeToCurrent = true;
	m_search[0] = '\0';
}

void ContentBrowser::EnumerateDirectory()
{
	m_entries.clear();
	m_selected = -1;
	std::error_code ec;
	if (m_rootsDirty) RefreshRoots();
	fs::path dir = fs::path(CurrentRoot().dir);
	if (!m_currentDir.empty()) dir /= m_currentDir;
	if (!fs::is_directory(dir, ec))
	{
		Log::Warn("콘텐츠 브라우저: 폴더가 없어 루트로 돌아감: %s", Log::ToUtf8(dir.c_str()).c_str());
		m_currentDir.clear();
		dir = fs::path(CurrentRoot().dir);
	}

	for (const fs::directory_entry& it : fs::directory_iterator(dir, ec))
	{
		Entry entry;
		entry.name = it.path().filename().wstring();
		entry.nameUtf8 = Log::ToUtf8(entry.name.c_str());
		entry.relativePath = m_currentDir.empty() ? entry.name : m_currentDir + L"\\" + entry.name;
		entry.absolutePath = it.path().wstring();
		entry.isDirectory = it.is_directory(ec);
		entry.type = Classify(entry.relativePath, entry.isDirectory);
		const std::wstring ext = Extension(entry.name);
		entry.hidden = !entry.isDirectory && (ext == L".bin" || ext == L".ini" || ext == L".log");
		entry.size = entry.isDirectory ? 0 : it.file_size(ec);
		m_entries.push_back(std::move(entry));
	}
	if (ec) Log::Warn("콘텐츠 브라우저: 폴더 열거 중 오류 (%s)", ec.message().c_str());

	std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b)
	{
		if (a.isDirectory != b.isDirectory) return a.isDirectory;
		return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
	});
	m_dirStamp = DirectoryStamp(dir);

	// 트리: 루트마다 하나 (Project / Engine)
	m_trees.clear();
	for (int r = 0; r < static_cast<int>(m_roots.size()); ++r)
	{
		TreeNode tree;
		tree.name = std::wstring(m_roots[r].label.begin(), m_roots[r].label.end());
		tree.nameUtf8 = m_roots[r].label;
		tree.root = r;
		BuildTree(tree, m_roots[r].dir, 0);
		m_trees.push_back(std::move(tree));
	}
	m_dirty = false;
}

void ContentBrowser::RefreshRoots()
{
	m_roots.clear();
	if (Paths::HasProject()) m_roots.push_back(Root{ "Project", Paths::GetAssetRoot() });
	m_roots.push_back(Root{ Paths::HasProject() ? "Engine" : "Engine (no project)", Paths::GetEngineAssetRoot() });
	if (m_currentRoot >= static_cast<int>(m_roots.size())) { m_currentRoot = 0; m_currentDir.clear(); }
	m_rootsDirty = false;
	ClearThumbnails();
	Log::Info("콘텐츠 브라우저 루트: %s", Log::ToUtf8(m_roots[0].dir.c_str()).c_str());
}

void ContentBrowser::BuildTree(TreeNode& node, const std::wstring& absoluteDir, int depth)
{
	if (depth >= kMaxTreeDepth) return;
	std::error_code ec;
	for (const fs::directory_entry& it : fs::directory_iterator(absoluteDir, ec))
	{
		if (!it.is_directory(ec)) continue;
		TreeNode child;
		child.name = it.path().filename().wstring();
		child.nameUtf8 = Log::ToUtf8(child.name.c_str());
		child.relativePath = node.relativePath.empty() ? child.name : node.relativePath + L"\\" + child.name;
		child.root = node.root;
		BuildTree(child, it.path().wstring(), depth + 1);
		node.children.push_back(std::move(child));
	}
	std::sort(node.children.begin(), node.children.end(), [](const TreeNode& a, const TreeNode& b) { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
}

// ------------------------------------------------------------------ 그리기

void ContentBrowser::Draw(RHI::Device& device, const Actions& actions)
{
	m_device = &device;
	++m_frame;
	if (m_rootsDirty) { RefreshRoots(); m_dirty = true; }
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_dragCancelled = false;

	// 1초마다 폴더 수정 시각을 보고 바뀌었으면 다시 읽는다 (탐색기에서 파일을 넣었을 때).
	const double now = ImGui::GetTime();
	if (!m_dirty && now - m_lastPollTime > 1.0)
	{
		m_lastPollTime = now;
		fs::path dir = fs::path(CurrentRoot().dir);
		if (!m_currentDir.empty()) dir /= m_currentDir;
		if (DirectoryStamp(dir) != m_dirStamp) m_dirty = true;
	}
	if (m_dirty) EnumerateDirectory();

	PumpThumbnailLoads(kThumbnailLoadsPerFrame);
	EvictThumbnails();

	DrawToolbar();

	const float statusHeight = ImGui::GetFrameHeightWithSpacing();
	ImGui::BeginChild("##tree", ImVec2(180.0f, -statusHeight), ImGuiChildFlags_ResizeX | ImGuiChildFlags_Borders);
	for (const TreeNode& tree : m_trees) DrawFolderTree(tree);
	m_expandTreeToCurrent = false;
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild("##grid", ImVec2(0.0f, -statusHeight), ImGuiChildFlags_Borders);
	DrawBreadcrumb();
	ImGui::Separator();
	DrawTileGrid(actions);
	ImGui::EndChild();

	// 상태줄
	int visible = 0;
	for (const Entry& e : m_entries) if (!e.hidden || m_showHidden) ++visible;
	if (m_selected >= 0 && m_selected < static_cast<int>(m_entries.size()))
	{
		const Entry& e = m_entries[m_selected];
		ImGui::Text("%d items   |   %s   %s%s   %s", visible, e.nameUtf8.c_str(), TypeName(e.type), e.isDirectory ? "" : ("   " + SizeText(e.size)).c_str(),
			Log::ToUtf8(e.relativePath.c_str()).c_str());
	}
	else
	{
		ImGui::Text("%d items   |   drag a model or scene onto the Scene view, double-click to open   |   thumbnails %zu", visible, m_thumbnails.size());
	}
}

void ContentBrowser::DrawToolbar()
{
	if (ImGui::Button("Up") && !m_currentDir.empty())
	{
		const size_t slash = m_currentDir.find_last_of(L'\\');
		SetDirectory(slash == std::wstring::npos ? L"" : m_currentDir.substr(0, slash), m_currentRoot);
	}
	ImGui::SameLine();
	if (ImGui::Button("Refresh")) { m_dirty = true; ClearThumbnails(); }
	ImGui::SameLine();
	ImGui::SetNextItemWidth(200.0f);
	ImGui::InputTextWithHint("##search", "search in folder", m_search, sizeof(m_search));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	ImGui::SliderFloat("##tile", &m_tileSize, 64.0f, 192.0f, "tile %.0f");
	ImGui::SameLine();
	ImGui::Checkbox("Hidden", &m_showHidden);
	ImGui::SameLine();
	ImGui::TextDisabled("%s", m_roots.empty() ? "" : Log::ToUtf8(CurrentRoot().dir.c_str()).c_str());
}

void ContentBrowser::DrawBreadcrumb()
{
	if (ImGui::SmallButton(m_roots.empty() ? "Assets" : CurrentRoot().label.c_str())) SetDirectory(L"", m_currentRoot);
	std::wstring prefix;
	size_t start = 0;
	int index = 0;
	while (start < m_currentDir.size())
	{
		size_t end = m_currentDir.find(L'\\', start);
		if (end == std::wstring::npos) end = m_currentDir.size();
		const std::wstring segment = m_currentDir.substr(start, end - start);
		prefix = prefix.empty() ? segment : prefix + L"\\" + segment;
		ImGui::SameLine(); ImGui::TextDisabled(">"); ImGui::SameLine();
		ImGui::PushID(index++);
		if (ImGui::SmallButton(Log::ToUtf8(segment.c_str()).c_str())) SetDirectory(prefix, m_currentRoot);
		ImGui::PopID();
		start = end + 1;
	}
}

void ContentBrowser::DrawFolderTree(const TreeNode& node)
{
	const bool leaf = node.children.empty();
	const bool sameRoot = node.root == m_currentRoot;
	const bool current = sameRoot && _wcsicmp(node.relativePath.c_str(), m_currentDir.c_str()) == 0;
	const bool ancestor = sameRoot && (node.relativePath.empty() || StartsWithNoCase(m_currentDir, node.relativePath + L"\\"));
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	if (current) flags |= ImGuiTreeNodeFlags_Selected;
	if (node.relativePath.empty()) flags |= ImGuiTreeNodeFlags_DefaultOpen;
	if (m_expandTreeToCurrent && ancestor && !leaf) ImGui::SetNextItemOpen(true);

	ImGui::PushID(node.nameUtf8.c_str());
	ImGui::PushID(node.root);
	const bool open = ImGui::TreeNodeEx("##node", flags, "%s", node.nameUtf8.c_str());
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) SetDirectory(node.relativePath, node.root);
	if (open && !leaf)
	{
		for (const TreeNode& child : node.children) DrawFolderTree(child);
		ImGui::TreePop();
	}
	ImGui::PopID();
	ImGui::PopID();
}

void ContentBrowser::DrawTileGrid(const Actions& actions)
{
	std::vector<int> visible;
	visible.reserve(m_entries.size());
	for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
	{
		const Entry& e = m_entries[i];
		if (e.hidden && !m_showHidden) continue;
		if (!ContainsNoCase(e.nameUtf8, m_search)) continue;
		visible.push_back(i);
	}
	if (visible.empty())
	{
		ImGui::TextDisabled(m_entries.empty() ? "(empty folder)" : "(no match)");
		return;
	}

	const ImGuiStyle& style = ImGui::GetStyle();
	const float gap = 8.0f;
	const ImVec2 tile(m_tileSize, m_tileSize + ImGui::GetTextLineHeight() + 10.0f);
	const int columns = (std::max)(1, static_cast<int>((ImGui::GetContentRegionAvail().x + gap) / (tile.x + gap)));
	const int rows = (static_cast<int>(visible.size()) + columns - 1) / columns;

	// 보이는 행만 제출한다 — 썸네일도 보이는 타일만 요청되므로 큰 폴더에서 로드 큐가 폭주하지 않는다.
	ImGuiListClipper clipper;
	clipper.Begin(rows, tile.y + style.ItemSpacing.y);
	while (clipper.Step())
	{
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
		{
			for (int col = 0; col < columns; ++col)
			{
				const int index = row * columns + col;
				if (index >= static_cast<int>(visible.size())) break;
				if (col > 0) ImGui::SameLine(0.0f, gap);
				DrawTile(visible[index], tile, actions);
			}
		}
	}
	clipper.End();

	// 빈 곳 클릭 → 선택 해제
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) m_selected = -1;
}

void ContentBrowser::DrawTile(int entryIndex, const ImVec2& tile, const Actions& actions)
{
	const Entry& entry = m_entries[entryIndex];
	ImGui::PushID(entryIndex);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##tile", tile);
	const bool hovered = ImGui::IsItemHovered();
	const bool selected = m_selected == entryIndex;
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) m_selected = entryIndex;
	if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) OnDoubleClick(entry, actions);

	// ---- 그림: 배경, 썸네일/아이콘, 배지, 라벨 ----
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 end(pos.x + tile.x, pos.y + tile.y);
	const float alpha = entry.hidden ? 0.45f : 1.0f;
	if (selected) dl->AddRectFilled(pos, end, ImGui::GetColorU32(ImGuiCol_Header), 6.0f);
	else if (hovered) dl->AddRectFilled(pos, end, ImGui::GetColorU32(ImGuiCol_HeaderHovered), 6.0f);
	else dl->AddRectFilled(pos, end, ImGui::GetColorU32(ImGuiCol_FrameBg, 0.5f), 6.0f);

	const float pad = 8.0f;
	const ImVec2 imgMin(pos.x + pad, pos.y + pad);
	const ImVec2 imgMax(pos.x + tile.x - pad, pos.y + m_tileSize - pad);
	uint64_t thumb = entry.type == AssetType::Texture ? RequestThumbnail(entry) : 0;
	if (thumb != 0)
	{
		// 레터박스: 원본 비율은 알 수 없으니(썸네일은 긴 변 128) 정사각형에 맞춘다. 비율은 축소 시 지켰다.
		dl->AddRectFilled(imgMin, imgMax, IM_COL32(60, 60, 68, static_cast<int>(255 * alpha)), 4.0f);
		dl->AddImageRounded(static_cast<ImTextureID>(thumb), imgMin, imgMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(255 * alpha)), 4.0f);
	}
	else
	{
		DrawTypeIcon(dl, imgMin, imgMax, entry.type, alpha);
	}

	if (!entry.isDirectory)
	{
		std::string badge = Log::ToUtf8(Extension(entry.name).c_str());
		if (!badge.empty()) badge.erase(0, 1);
		std::transform(badge.begin(), badge.end(), badge.begin(), [](char c) { return static_cast<char>(::toupper(static_cast<unsigned char>(c))); });
		if (!badge.empty())
		{
			const ImVec2 size = ImGui::CalcTextSize(badge.c_str());
			const ImVec2 bMax(imgMax.x - 2.0f, imgMax.y - 2.0f);
			const ImVec2 bMin(bMax.x - size.x - 6.0f, bMax.y - size.y - 2.0f);
			dl->AddRectFilled(bMin, bMax, IM_COL32(20, 22, 40, static_cast<int>(200 * alpha)), 3.0f);
			dl->AddText(ImVec2(bMin.x + 3.0f, bMin.y + 1.0f), IM_COL32(240, 240, 250, static_cast<int>(255 * alpha)), badge.c_str());
		}
	}

	const std::string label = TruncateLabel(entry.nameUtf8, tile.x - 8.0f);
	const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
	const ImU32 textColor = ImGui::GetColorU32(entry.hidden ? ImGuiCol_TextDisabled : ImGuiCol_Text);
	dl->AddText(ImVec2(pos.x + (tile.x - labelSize.x) * 0.5f, pos.y + m_tileSize + 2.0f), textColor, label.c_str());

	// ---- 상호작용: 드래그 소스 → 컨텍스트 메뉴 → 툴팁 (마지막 아이템 ID 가 InvisibleButton 인 동안) ----
	BeginDragSource(entry);
	DrawContextMenu(entry, actions);
	if (hovered && ImGui::GetDragDropPayload() == nullptr)
	{
		ImGui::SetTooltip("%s\n%s%s\n%s", entry.nameUtf8.c_str(), TypeName(entry.type), entry.isDirectory ? "" : ("   " + SizeText(entry.size)).c_str(),
			Log::ToUtf8(entry.relativePath.c_str()).c_str());
	}
	ImGui::PopID();
}

void ContentBrowser::BeginDragSource(const Entry& entry)
{
	if (entry.isDirectory || entry.type == AssetType::Other) return;
	if (entry.relativePath.size() >= 260)
	{
		static bool warned = false;
		if (!warned) { warned = true; Log::Warn("콘텐츠 브라우저: 경로가 260자 이상이라 드래그할 수 없음: %s", entry.nameUtf8.c_str()); }
		return;
	}
	if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) return;

	AssetDragPayload payload;
	payload.type = entry.type;
	wcsncpy_s(payload.relativePath, entry.relativePath.c_str(), _TRUNCATE);
	ImGui::SetDragDropPayload(kAssetPayloadType, &payload, sizeof(payload), ImGuiCond_Once);

	// 미리보기: 썸네일/아이콘 + 이름 + 타입
	const float previewSize = 48.0f;
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::Dummy(ImVec2(previewSize, previewSize));
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const uint64_t thumb = entry.type == AssetType::Texture ? RequestThumbnail(entry) : 0;
	if (thumb != 0) dl->AddImageRounded(static_cast<ImTextureID>(thumb), pos, ImVec2(pos.x + previewSize, pos.y + previewSize), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 4.0f);
	else DrawTypeIcon(dl, pos, ImVec2(pos.x + previewSize, pos.y + previewSize), entry.type);
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::TextUnformatted(entry.nameUtf8.c_str());
	ImGui::TextDisabled("%s", TypeName(entry.type));
	if (m_dragCancelled) ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "cancelled (release)");
	else ImGui::TextDisabled(entry.type == AssetType::Scene ? "drop on Scene view: load" : "drop on Scene view   Esc: cancel");
	ImGui::EndGroup();

	if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_dragCancelled = true;
	ImGui::EndDragDropSource();
}

void ContentBrowser::DrawContextMenu(const Entry& entry, const Actions& actions)
{
	if (!ImGui::BeginPopupContextItem("##ctx")) return;
	ImGui::TextDisabled("%s", entry.nameUtf8.c_str());
	ImGui::Separator();
	if (entry.isDirectory && ImGui::MenuItem("Open folder")) SetDirectory(entry.relativePath, m_currentRoot);
	if (entry.type == AssetType::Model && ImGui::MenuItem("Place at origin") && actions.placeModel) actions.placeModel(entry.relativePath, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
	if (entry.type == AssetType::Scene && ImGui::MenuItem("Load scene") && actions.openScene) actions.openScene(entry.relativePath);
	if (ImGui::MenuItem("Copy relative path")) ImGui::SetClipboardText(Log::ToUtf8(entry.relativePath.c_str()).c_str());
	if (ImGui::MenuItem("Show in Explorer"))
	{
		const std::wstring parameters = L"/select,\"" + entry.absolutePath + L"\"";
		ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
	}
	ImGui::EndPopup();
}

void ContentBrowser::OnDoubleClick(const Entry& entry, const Actions& actions)
{
	switch (entry.type)
	{
	case AssetType::Folder: SetDirectory(entry.relativePath, m_currentRoot); break;
	case AssetType::Scene: if (actions.openScene) actions.openScene(entry.relativePath); break;
	case AssetType::Model: if (actions.placeModel) actions.placeModel(entry.relativePath, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f)); break;
	default: break;
	}
}

// ------------------------------------------------------------------ 썸네일

uint64_t ContentBrowser::RequestThumbnail(const Entry& entry)
{
	if (entry.type != AssetType::Texture || m_device == nullptr) return 0;
	auto it = m_thumbnails.find(entry.absolutePath);
	if (it == m_thumbnails.end())
	{
		Thumbnail t;
		t.lastUsedFrame = m_frame;
		m_thumbnails.emplace(entry.absolutePath, t);
		m_pending.push_back(entry.absolutePath);
		return 0;
	}
	it->second.lastUsedFrame = m_frame;
	if (it->second.failed || !it->second.handle.IsValid()) return 0;
	return m_device->GetImGuiTextureId(it->second.handle);
}

void ContentBrowser::PumpThumbnailLoads(uint32_t budget)
{
	while (budget > 0 && !m_pending.empty())
	{
		const std::wstring absolute = m_pending.front();
		m_pending.pop_front();
		auto it = m_thumbnails.find(absolute);
		if (it == m_thumbnails.end()) continue;                    // 그 사이 지워짐
		if (m_frame - it->second.lastUsedFrame > 2)                // 스크롤로 사라진 타일. 다시 보이면 다시 요청된다
		{
			m_thumbnails.erase(it);
			continue;
		}
		TextureImage image;
		if (!TextureLoader::LoadThumbnail(absolute, kThumbnailSize, image))
		{
			it->second.failed = true;
			--budget;
			continue;
		}
		image.desc.debugName = "Thumbnail";
		it->second.handle = m_device->CreateTexture(image.desc, image.subresources.data(), static_cast<uint32_t>(image.subresources.size()));
		if (!it->second.handle.IsValid()) it->second.failed = true;
		--budget;
	}
}

void ContentBrowser::EvictThumbnails()
{
	if (m_thumbnails.size() <= kMaxThumbnails || m_device == nullptr) return;
	std::vector<std::pair<uint64_t, std::wstring>> candidates;
	for (const auto& entry : m_thumbnails)
	{
		if (entry.second.lastUsedFrame != m_frame) candidates.emplace_back(entry.second.lastUsedFrame, entry.first);
	}
	std::sort(candidates.begin(), candidates.end());
	for (const auto& candidate : candidates)
	{
		if (m_thumbnails.size() <= kEvictTo) break;
		auto it = m_thumbnails.find(candidate.second);
		if (it == m_thumbnails.end()) continue;
		if (it->second.handle.IsValid()) m_device->DestroyTexture(it->second.handle);
		m_thumbnails.erase(it);
	}
}

void ContentBrowser::ClearThumbnails()
{
	if (m_device != nullptr)
	{
		for (auto& entry : m_thumbnails)
		{
			if (entry.second.handle.IsValid()) m_device->DestroyTexture(entry.second.handle);
		}
	}
	m_thumbnails.clear();
	m_pending.clear();
}

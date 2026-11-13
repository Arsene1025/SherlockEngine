#include "pch.h"
#include "Graphics/TextureImporter.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXTex.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

namespace
{
	namespace fs = std::filesystem;
	using Microsoft::WRL::ComPtr;

	constexpr uint32_t kConverterVersion = 1;    // 변환 방식(필터·인코더)이 바뀌면 올려 캐시를 무효화함
	constexpr uint32_t kMinCompressSize = 256;   // 기본 설정일 때 이보다 작은 텍스처는 변환하지 않음 (UI·아이콘·1×1)

	bool g_buildEnabled = false;
	std::mutex g_mutex;
	std::map<std::string, TextureImportSettings> g_settings;
	std::wstring g_settingsLoadedFor;   // g_settings 를 읽은 설정 파일 경로

	std::wstring SettingsPath() { return Paths::GetAssetRoot() + L"TextureSettings.json"; }

	void EnsureLoaded()   // g_mutex 를 잡은 채 호출
	{
		const std::wstring path = SettingsPath();
		if (path == g_settingsLoadedFor) return;
		g_settingsLoadedFor = path;
		g_settings.clear();
		std::ifstream file{ fs::path(path) };
		if (!file) return;
		try
		{
			const nlohmann::json root = nlohmann::json::parse(file);
			if (!root.contains("textures")) return;
			for (const auto& item : root["textures"].items())
			{
				TextureImportSettings s;
				s.compress = item.value().value("compress", true);
				s.mipLevels = item.value().value("mipLevels", 0u);
				s.maxSize = item.value().value("maxSize", 0u);
				g_settings[item.key()] = s;
			}
		}
		catch (const std::exception& e) { Log::Warn("TextureSettings.json 을 읽지 못함: %s", e.what()); }
	}

	void Save()   // g_mutex 를 잡은 채 호출
	{
		const fs::path path = SettingsPath();
		std::error_code ec;
		if (g_settings.empty()) { fs::remove(path, ec); return; }
		nlohmann::json textures = nlohmann::json::object();
		for (const auto& [name, s] : g_settings)
			textures[name] = { { "compress", s.compress }, { "mipLevels", s.mipLevels }, { "maxSize", s.maxSize } };
		fs::create_directories(path.parent_path(), ec);
		std::ofstream file{ path };
		if (file) file << nlohmann::json{ { "textures", textures } }.dump(2);
	}

	uint64_t Fnv(const void* data, size_t size, uint64_t hash = 1469598103934665603ull)
	{
		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
		return hash;
	}

	// GPU BC7 인코더용 임시 D3D11 장치 (렌더러의 장치와 별개 — D3D12 백엔드에서도 씀). 못 만들면 CPU 로 압축함.
	ID3D11Device* GetGpuDevice()
	{
		static ComPtr<ID3D11Device> device;
		static bool tried = false;
		if (!tried)
		{
			tried = true;
			const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, nullptr);
			if (FAILED(hr)) Log::Warn("텍스처 변환: GPU 장치를 만들지 못해 CPU 로 압축함.");
		}
		return device.Get();
	}

	bool IsDds(const uint8_t* data, size_t size) { return size >= 4 && data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' '; }

	// 원본 → 설정에 맞춘 DDS. 실패하면 false (호출자가 원본을 그대로 씀).
	bool Build(const uint8_t* data, size_t size, bool srgb, const TextureImportSettings& s, const fs::path& outPath, std::string& summary)
	{
		using namespace DirectX;
		const auto start = std::chrono::steady_clock::now();
		ScratchImage image;
		HRESULT hr = LoadFromWICMemory(data, size, srgb ? WIC_FLAGS_DEFAULT_SRGB : WIC_FLAGS_IGNORE_SRGB, nullptr, image);
		if (FAILED(hr)) return false;

		const DXGI_FORMAT rgba = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
		const TEX_FILTER_FLAGS filter = srgb ? TEX_FILTER_SRGB : TEX_FILTER_DEFAULT;
		ScratchImage work;
		if (image.GetMetadata().format != rgba)
		{
			if (FAILED(Convert(image.GetImages(), image.GetImageCount(), image.GetMetadata(), rgba, filter, TEX_THRESHOLD_DEFAULT, work))) return false;
		}
		else work = std::move(image);

		// 밉 체인 + 해상도 축소. 전체 체인을 만든 뒤 maxSize 를 넘는 위쪽 단계를 건너뛰고(= 한 단계 아래 밉이 그대로 새 원본이 됨), 남은 체인을
		// mipLevels 로 자름. mipLevels 1 + maxSize 0 이면 체인이 필요 없음.
		if (s.mipLevels != 1 || s.maxSize > 0)
		{
			ScratchImage chain;
			if (work.GetMetadata().width > 1 || work.GetMetadata().height > 1)
			{
				hr = GenerateMipMaps(work.GetImages(), work.GetImageCount(), work.GetMetadata(), filter, 0, chain);
				if (FAILED(hr)) { Log::Warn("텍스처 변환: 밉 생성 실패 (%s)", Log::HrToString(hr).c_str()); return false; }
			}
			else chain = std::move(work);
			const TexMetadata& cm = chain.GetMetadata();
			size_t skip = 0;
			while (s.maxSize > 0 && skip + 1 < cm.mipLevels && (std::max)(chain.GetImage(skip, 0, 0)->width, chain.GetImage(skip, 0, 0)->height) > s.maxSize) ++skip;
			size_t keep = cm.mipLevels - skip;
			if (s.mipLevels > 0) keep = (std::min)(keep, static_cast<size_t>(s.mipLevels));
			ScratchImage trimmed;
			const Image* top = chain.GetImage(skip, 0, 0);
			hr = trimmed.Initialize2D(cm.format, top->width, top->height, 1, keep);
			if (FAILED(hr)) return false;
			for (size_t level = 0; level < keep; ++level)
			{
				const Image* from = chain.GetImage(skip + level, 0, 0);
				const Image* to = trimmed.GetImage(level, 0, 0);
				if (from == nullptr || to == nullptr || from->slicePitch != to->slicePitch) return false;
				std::memcpy(to->pixels, from->pixels, from->slicePitch);
			}
			work = std::move(trimmed);
		}

		const TexMetadata& meta = work.GetMetadata();
		const char* format = "RGBA8";
		ScratchImage compressed;
		const ScratchImage* result = &work;
		if (s.compress && meta.width % 4 == 0 && meta.height % 4 == 0)
		{
			const DXGI_FORMAT bc7 = srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
			hr = E_FAIL;
			if (ID3D11Device* gpu = GetGpuDevice())
				hr = Compress(gpu, work.GetImages(), work.GetImageCount(), meta, bc7, srgb ? TEX_COMPRESS_SRGB_IN : TEX_COMPRESS_DEFAULT, 1.0f, compressed);
			if (FAILED(hr))
				hr = Compress(work.GetImages(), work.GetImageCount(), meta, bc7, TEX_COMPRESS_BC7_QUICK | TEX_COMPRESS_PARALLEL | (srgb ? TEX_COMPRESS_SRGB_IN : TEX_COMPRESS_DEFAULT), TEX_THRESHOLD_DEFAULT, compressed);
			if (FAILED(hr)) return false;
			result = &compressed;
			format = "BC7";
		}

		std::error_code ec;
		fs::create_directories(outPath.parent_path(), ec);
		const fs::path temp = fs::path(outPath).concat(L".tmp");   // 쓰다 중단되어도 깨진 캐시가 남지 않게 임시 이름으로 쓴 뒤 바꿈
		if (FAILED(SaveToDDSFile(result->GetImages(), result->GetImageCount(), result->GetMetadata(), DDS_FLAGS_NONE, temp.c_str()))) return false;
		fs::rename(temp, outPath, ec);
		if (ec) { fs::remove(temp, ec); return false; }

		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		char buffer[160];
		snprintf(buffer, sizeof(buffer), "%zux%zu %s, 밉 %zu, %.1f초", result->GetMetadata().width, result->GetMetadata().height, format, result->GetMetadata().mipLevels, seconds);
		summary = buffer;
		return true;
	}
}

void TextureImporter::SetBuildEnabled(bool enabled) { g_buildEnabled = enabled; }
bool TextureImporter::IsBuildEnabled() { return g_buildEnabled; }

TextureImportSettings TextureImporter::GetSettings(const std::string& name)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	EnsureLoaded();
	const auto found = g_settings.find(name);
	return found != g_settings.end() ? found->second : TextureImportSettings{};
}

void TextureImporter::SetSettings(const std::string& name, const TextureImportSettings& settings)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	EnsureLoaded();
	if (settings.IsDefault()) g_settings.erase(name);
	else g_settings[name] = settings;
	Save();
}

std::wstring TextureImporter::GetCacheDir()
{
	// 배포본(엔진 소스 트리 없음)은 .sherlock 이 exe 옆에 있어 HasProject() 가 true 여도 GameBuilder 가 캐시를 <exe>\TextureCache\ 에 싣는다.
	if (Paths::HasProject() && Paths::HasEngineSourceTree()) return Paths::GetProjectRoot() + L"Library\\TextureCache\\";
	return Paths::GetExecutableDir() + L"TextureCache\\";
}

std::wstring TextureImporter::ResolveCached(const std::string& name, const uint8_t* data, size_t size, bool srgb)
{
	if (data == nullptr || size == 0 || IsDds(data, size)) return L"";   // DDS 는 사용자가 이미 만든 것 — 그대로 읽음
	const TextureImportSettings settings = GetSettings(name);

	// 기본 설정이고 작은 텍스처면 변환하지 않음. 크기는 헤더만 읽어 앎.
	DirectX::TexMetadata meta;
	if (FAILED(DirectX::GetMetadataFromWICMemory(data, size, DirectX::WIC_FLAGS_NONE, meta))) return L"";
	if (settings.IsDefault() && (std::max)(meta.width, meta.height) < kMinCompressSize) return L"";

	// 키: 원본 내용 + 색공간 + 설정 + 변환기 버전. 원본이 바뀌거나 설정이 바뀌면 다른 파일이 됨 (옛 파일은 남음 — 캐시 폴더를 지우면 정리됨).
	uint64_t hash = Fnv(data, size);
	const uint32_t key[5] = { srgb ? 1u : 0u, settings.compress ? 1u : 0u, settings.mipLevels, settings.maxSize, kConverterVersion };
	hash = Fnv(key, sizeof(key), hash);
	wchar_t file[32];
	swprintf_s(file, L"%016llx.dds", static_cast<unsigned long long>(hash));
	const fs::path path = fs::path(GetCacheDir()) / file;

	std::error_code ec;
	if (fs::exists(path, ec)) return path.wstring();
	if (!g_buildEnabled) return L"";

	std::string summary;
	if (!Build(data, size, srgb, settings, path, summary))
	{
		Log::Warn("텍스처 변환 실패: %s (원본을 그대로 씀)", name.c_str());
		return L"";
	}
	Log::Info("텍스처 변환: %s (%s)", name.c_str(), summary.c_str());
	return path.wstring();
}

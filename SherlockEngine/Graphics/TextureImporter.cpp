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

	std::wstring SettingsPath()
	{
		return Paths::GetAssetRoot() + L"TextureSettings.json";
	}

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
		catch (const std::exception& e)
		{
			Log::Warn("TextureSettings.json 을 읽지 못함: %s", e.what());
		}
	}

	void Save()   // g_mutex 를 잡은 채 호출
	{
		const fs::path path = SettingsPath();
		std::error_code ec;
		if (g_settings.empty())
		{
			fs::remove(path, ec);
			return;
		}
		nlohmann::json textures = nlohmann::json::object();
		for (const auto& [name, s] : g_settings)
		{
			textures[name] = { { "compress", s.compress }, { "mipLevels", s.mipLevels }, { "maxSize", s.maxSize } };
		}
		fs::create_directories(path.parent_path(), ec);
		std::ofstream file{ path };
		if (file)
		{
			file << nlohmann::json{ { "textures", textures } }.dump(2);
		}
	}

	uint64_t Fnv(const void* data, size_t size, uint64_t hash = 1469598103934665603ull)
	{
		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i)
		{
			hash ^= bytes[i];
			hash *= 1099511628211ull;
		}
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
			if (FAILED(hr))
			{
				Log::Warn("텍스처 변환: GPU 장치를 만들지 못해 CPU 로 압축함.");
			}
		}
		return device.Get();
	}

	bool IsDds(const uint8_t* data, size_t size)
	{
		return size >= 4 && data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ';
	}

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
		else
		{
			work = std::move(image);
		}

		// 밉 체인 + 해상도 축소. 전체 체인을 만든 뒤 maxSize 를 넘는 위쪽 단계를 건너뛰고(= 한 단계 아래 밉이 그대로 새 원본이 됨), 남은 체인을
		// mipLevels 로 자름. mipLevels 1 + maxSize 0 이면 체인이 필요 없음.
		if (s.mipLevels != 1 || s.maxSize > 0)
		{
			ScratchImage chain;
			if (work.GetMetadata().width > 1 || work.GetMetadata().height > 1)
			{
				hr = GenerateMipMaps(work.GetImages(), work.GetImageCount(), work.GetMetadata(), filter, 0, chain);
				if (FAILED(hr))
				{
					Log::Warn("텍스처 변환: 밉 생성 실패 (%s)", Log::HrToString(hr).c_str());
					return false;
				}
			}
			else
			{
				chain = std::move(work);
			}
			const TexMetadata& cm = chain.GetMetadata();
			size_t skip = 0;
			while (s.maxSize > 0 && skip + 1 < cm.mipLevels && (std::max)(chain.GetImage(skip, 0, 0)->width, chain.GetImage(skip, 0, 0)->height) > s.maxSize)
			{
				++skip;
			}
			size_t keep = cm.mipLevels - skip;
			if (s.mipLevels > 0)
			{
				keep = (std::min)(keep, static_cast<size_t>(s.mipLevels));
			}
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
			{
				hr = Compress(gpu, work.GetImages(), work.GetImageCount(), meta, bc7, srgb ? TEX_COMPRESS_SRGB_IN : TEX_COMPRESS_DEFAULT, 1.0f, compressed);
			}
			if (FAILED(hr))
			{
				hr = Compress(work.GetImages(), work.GetImageCount(), meta, bc7, TEX_COMPRESS_BC7_QUICK | TEX_COMPRESS_PARALLEL | (srgb ? TEX_COMPRESS_SRGB_IN : TEX_COMPRESS_DEFAULT), TEX_THRESHOLD_DEFAULT, compressed);
			}
			if (FAILED(hr)) return false;
			result = &compressed;
			format = "BC7";
		}

		std::error_code ec;
		fs::create_directories(outPath.parent_path(), ec);
		const fs::path temp = fs::path(outPath).concat(L".tmp");   // 쓰다 중단되어도 깨진 캐시가 남지 않게 임시 이름으로 쓴 뒤 바꿈
		if (FAILED(SaveToDDSFile(result->GetImages(), result->GetImageCount(), result->GetMetadata(), DDS_FLAGS_NONE, temp.c_str()))) return false;
		fs::rename(temp, outPath, ec);
		if (ec)
		{
			fs::remove(temp, ec);
			return false;
		}

		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		char buffer[160];
		snprintf(buffer, sizeof(buffer), "%zux%zu %s, 밉 %zu, %.1f초", result->GetMetadata().width, result->GetMetadata().height, format, result->GetMetadata().mipLevels, seconds);
		summary = buffer;
		return true;
	}
}

void TextureImporter::SetBuildEnabled(bool enabled)
{
	g_buildEnabled = enabled;
}
bool TextureImporter::IsBuildEnabled()
{
	return g_buildEnabled;
}

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
	if (settings.IsDefault())
	{
		g_settings.erase(name);
	}
	else
	{
		g_settings[name] = settings;
	}
	Save();
}

std::wstring TextureImporter::GetCacheDir()
{
	// 배포본(엔진 소스 트리 없음)은 .sherlock 이 exe 옆에 있어 HasProject() 가 true 여도 GameBuilder 가 캐시를 <exe>\TextureCache\ 에 싣는다.
	if (Paths::HasProject() && Paths::HasEngineSourceTree()) return Paths::GetProjectRoot() + L"Library\\TextureCache\\";
	return Paths::GetExecutableDir() + L"TextureCache\\";
}

namespace
{
	// cook = 게임 빌드용: 기본 설정의 작은 텍스처도 압축 없이(R8G8B8A8 + 밉) 변환함.
	std::wstring Resolve(const std::string& name, const uint8_t* data, size_t size, bool srgb, bool cook)
	{
		if (data == nullptr || size == 0 || IsDds(data, size)) return L"";   // DDS 는 사용자가 이미 만든 것 — 그대로 읽음
		TextureImportSettings settings = TextureImporter::GetSettings(name);

		// 기본 설정이고 작은 텍스처면 변환하지 않음 (빌드용이면 압축만 끔). 크기는 헤더만 읽어 앎.
		DirectX::TexMetadata meta;
		if (FAILED(DirectX::GetMetadataFromWICMemory(data, size, DirectX::WIC_FLAGS_NONE, meta))) return L"";
		if (settings.IsDefault() && (std::max)(meta.width, meta.height) < kMinCompressSize)
		{
			if (!cook) return L"";
			settings.compress = false;
		}

		// 키: 원본 내용 + 색공간 + 설정 + 변환기 버전. 원본이 바뀌거나 설정이 바뀌면 다른 파일이 됨 (옛 파일은 남음 — 캐시 폴더를 지우면 정리됨).
		uint64_t hash = Fnv(data, size);
		const uint32_t key[5] = { srgb ? 1u : 0u, settings.compress ? 1u : 0u, settings.mipLevels, settings.maxSize, kConverterVersion };
		hash = Fnv(key, sizeof(key), hash);
		wchar_t file[32];
		swprintf_s(file, L"%016llx.dds", static_cast<unsigned long long>(hash));
		const fs::path path = fs::path(TextureImporter::GetCacheDir()) / file;

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

	// ---- 게임 패키지의 변환 목록 ----
	// manifest.json: { "version": 1, "textures": { "<이름>": { "srgb": "<해시>.dds", "linear": "<해시>.dds" } } } (쓰이는 색공간만 있음)
	constexpr int kManifestVersion = 1;
	struct CookedPair
	{
		std::wstring srgb, linear;   // 캐시 폴더 기준 절대 경로. 없으면 빈 문자열
	};
	std::map<std::string, CookedPair> g_manifest;
	std::wstring g_manifestLoadedFor;   // g_manifest 를 읽은 캐시 폴더

	std::wstring Utf8ToWide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1)
		{
			MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
		}
		return wide;
	}

	// g_mutex 를 잡은 채 호출. 에디터(변환하는 쪽)는 목록을 쓰지 않음 — 항상 원본에서 해시 캐시를 찾음.
	const std::map<std::string, CookedPair>& Manifest()
	{
		if (g_buildEnabled)
		{
			static const std::map<std::string, CookedPair> empty;
			return empty;
		}
		const std::wstring dir = TextureImporter::GetCacheDir();
		if (dir == g_manifestLoadedFor) return g_manifest;
		g_manifestLoadedFor = dir;
		g_manifest.clear();
		std::ifstream file{ fs::path(dir) / L"manifest.json" };
		if (!file) return g_manifest;
		try
		{
			const nlohmann::json root = nlohmann::json::parse(file);
			if (root.value("version", 0) != kManifestVersion)
			{
				Log::Warn("TextureCache\\manifest.json 버전이 다름 — 무시하고 원본을 읽음.");
				return g_manifest;
			}
			// value() 는 값(임시 객체)을 돌려줌 — 지역 변수에 받아 둬야 함. 임시 객체에 바로 .items() 를 돌리면 range-for 가 시작하기 전에
			// 그 객체가 사라짐 (C++20 까지는 range-for 가 바깥 임시 객체만 살려 둠).
			const nlohmann::json textures = root.value("textures", nlohmann::json::object());
			for (const auto& item : textures.items())
			{
				CookedPair pair;
				const std::string srgb = item.value().value("srgb", "");
				const std::string linear = item.value().value("linear", "");
				pair.srgb = srgb.empty() ? L"" : dir + Utf8ToWide(srgb);
				pair.linear = linear.empty() ? L"" : dir + Utf8ToWide(linear);
				g_manifest[item.key()] = pair;
			}
			Log::Info("텍스처 변환 목록: %zu 개 (%s)", g_manifest.size(), Log::ToUtf8(dir.c_str()).c_str());
		}
		catch (const std::exception& e)
		{
			Log::Warn("TextureCache\\manifest.json 을 읽지 못함: %s", e.what());
			g_manifest.clear();
		}
		return g_manifest;
	}
}

std::wstring TextureImporter::ResolveCached(const std::string& name, const uint8_t* data, size_t size, bool srgb)
{
	return Resolve(name, data, size, srgb, false);
}

std::wstring TextureImporter::CookForBuild(const std::string& name, const uint8_t* data, size_t size, bool srgb)
{
	return Resolve(name, data, size, srgb, true);
}

bool TextureImporter::WriteManifest(const std::wstring& dir, const std::vector<CookedTexture>& textures, std::string& error)
{
	nlohmann::json entries = nlohmann::json::object();
	for (const CookedTexture& texture : textures)
	{
		entries[texture.name][texture.srgb ? "srgb" : "linear"] = Log::ToUtf8(texture.file.c_str());
	}
	std::error_code ec;
	fs::create_directories(dir, ec);
	std::ofstream file{ fs::path(dir) / L"manifest.json", std::ios::binary | std::ios::trunc };
	if (!file)
	{
		error = "cannot write " + Log::ToUtf8((fs::path(dir) / L"manifest.json").c_str());
		return false;
	}
	file << nlohmann::json{ { "version", kManifestVersion }, { "textures", entries } }.dump(2);
	return static_cast<bool>(file);
}

std::wstring TextureImporter::FindCooked(const std::string& name, bool srgb)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	const auto& manifest = Manifest();
	const auto found = manifest.find(name);
	if (found == manifest.end()) return L"";
	const std::wstring& exact = srgb ? found->second.srgb : found->second.linear;
	if (!exact.empty()) return exact;
	const std::wstring& other = srgb ? found->second.linear : found->second.srgb;
	if (!other.empty())
	{
		Log::Warn("텍스처 '%s' 의 %s 변환본이 패키지에 없어 %s 변환본을 씀 (빌드 때 재질에서 이 색공간으로 쓰이지 않았음).", name.c_str(), srgb ? "sRGB" : "linear", srgb ? "linear" : "sRGB");
	}
	return other;
}

bool TextureImporter::HasCooked(const std::string& name)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	const auto& manifest = Manifest();
	return manifest.find(name) != manifest.end();
}

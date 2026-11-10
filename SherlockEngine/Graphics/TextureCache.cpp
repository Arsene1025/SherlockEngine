#include "pch.h"
#include "Graphics/TextureCache.h"
#include "Graphics/TextureLoader.h"
#include "RHI/Device.h"
#include "RHI/ResourceDesc.h"
#include "Scene/Scene.h"
#include "Core/Paths.h"
#include "Core/Log.h"
#include <algorithm>
#include <cmath>

namespace
{
	std::wstring ToWide(const std::string& utf8)
	{
		if (utf8.empty()) return L"";
		const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
		if (length > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], length);
		return wide;
	}

	bool IsBuiltin(const std::string& name)
	{
		return name.rfind("builtin:", 0) == 0;
	}
}

bool TextureCache::Initialize(RHI::Device* device)
{
	m_device = device;
	if (m_device == nullptr) return false;
	m_white = Load2D("builtin:white", false, nullptr);
	m_flatNormal = Load2D("builtin:flatnormal", false, nullptr);
	m_blackCube = LoadCube("builtin:black");   // 환경이 없을 때 t9~t11 의 기본값
	return m_white.IsValid() && m_flatNormal.IsValid() && m_blackCube.IsValid();
}

void TextureCache::Shutdown()
{
	if (m_device == nullptr) return;
	// 로드에 실패한 항목은 흰색(또는 검은 큐브) 핸들을 공유하므로, 그 핸들은 건너뛰고 끝에 한 번만 지움.
	for (auto& cache : m_2d)
	{
		for (auto& entry : cache)
		{
			if (entry.second != m_white) m_device->DestroyTexture(entry.second);
		}
		cache.clear();
	}
	m_device->DestroyTexture(m_white);
	m_white = TextureHandle{};
	m_flatNormal = TextureHandle{};   // 위 루프가 캐시 항목으로 지웠음
	for (auto& entry : m_cubes)
	{
		if (entry.second.handle != m_blackCube) m_device->DestroyTexture(entry.second.handle);
	}
	m_cubes.clear();
	m_device->DestroyTexture(m_blackCube);
	m_blackCube = TextureHandle{};
	m_device = nullptr;
}

void TextureCache::ReleaseLoaded()
{
	if (m_device == nullptr) return;
	// 내장 텍스처("builtin:")는 남김 — 흰색·평평한 노멀은 프레임 셋과 기본 재질이 계속 씀.
	for (auto& cache : m_2d)
	{
		for (auto it = cache.begin(); it != cache.end();)
		{
			if (IsBuiltin(it->first)) { ++it; continue; }
			// 로드에 실패해 흰색으로 대체된 항목은 내장 핸들을 공유하므로 파괴하지 않음.
			if (it->second != m_white && it->second != m_flatNormal) m_device->DestroyTexture(it->second);
			it = cache.erase(it);
		}
	}
	// 파일에서 읽은 큐브맵도 버림 (내장 큐브는 남김).
	for (auto it = m_cubes.begin(); it != m_cubes.end();)
	{
		if (IsBuiltin(it->first)) { ++it; continue; }
		if (it->second.handle != m_blackCube) m_device->DestroyTexture(it->second.handle);
		it = m_cubes.erase(it);
	}
	++m_generation;   // 핸들을 기억해 둔 쪽(재질 셋)이 다음 프레임에 다시 찾게 함
}

TextureHandle TextureCache::Load2D(const std::string& name, bool srgb, const Scene* scene)
{
	// 빈 이름 = 텍스처 없음. 캐시 항목을 따로 만들지 않고 기본 흰색을 그대로 씀.
	if (name.empty() && m_white.IsValid())
	{
		return m_white;
	}
	// 색공간별로 맵을 따로 둠 — 조회할 때 "이름|srgb" 같은 키 문자열을 새로 만들지 않음.
	auto& cache = m_2d[srgb ? 1 : 0];
	auto found = cache.find(name);
	if (found != cache.end())
	{
		return found->second;
	}

	TextureImage image;
	bool ok = false;
	std::string debugName = name;
	if (name.empty() || name == "builtin:white")
	{
		const uint8_t white[4] = { 255, 255, 255, 255 };
		ok = TextureLoader::CreateSolid(1, white, false, image);   // 흰색은 sRGB든 선형이든 1.0
		debugName = "builtin:white";
	}
	else if (name == "builtin:flatnormal")
	{
		// 탄젠트 공간 (0, 0, 1) = 평평한 노멀. 항상 선형.
		const uint8_t flat[4] = { 128, 128, 255, 255 };
		ok = TextureLoader::CreateSolid(1, flat, false, image);
	}
	else if (name == "builtin:gray128")
	{
		// 감마 검증용. sRGB 128 → 선형 0.216 → sRGB 뷰에 쓰면 다시 128.
		const uint8_t gray[4] = { 128, 128, 128, 255 };
		ok = TextureLoader::CreateSolid(4, gray, srgb, image);
	}
	else if (name == "builtin:checker")
	{
		const uint8_t a[4] = { 230, 230, 230, 255 };
		const uint8_t b[4] = { 40, 40, 48, 255 };
		ok = TextureLoader::CreateChecker(512, 16, a, b, srgb, true, image);
	}
	else if (const std::vector<uint8_t>* encoded = scene != nullptr ? scene->FindImage(name) : nullptr)
	{
		// 모델에서 가져온 이미지. 씬이 인코딩된 바이트를 들고 있고, 여기서 재질의 색공간에 맞춰 디코딩함.
		ok = TextureLoader::LoadFromMemory(encoded->data(), encoded->size(), srgb, true, image);
		if (ok) Log::Info("텍스처 디코딩: %s (%ux%u, 밉 %u, %s)", name.c_str(), image.desc.width, image.desc.height, image.desc.mipLevels, srgb ? "sRGB" : "linear");
	}
	else
	{
		// "asset:<Assets 기준 상대 경로>" 는 Textures\ 밖의 파일을 가리킴 (콘텐츠 브라우저에서 끌어다 놓은
		// Sponza 이미지 등).
		static const char* kAssetPrefix = "asset:";
		const std::wstring path = name.rfind(kAssetPrefix, 0) == 0
			? Paths::GetAssetPath(ToWide(name.substr(6)).c_str())
			: Paths::GetAssetPath((L"Textures\\" + ToWide(name)).c_str());
		ok = TextureLoader::LoadFromFile(path, srgb, true, image);
	}

	TextureHandle handle;
	if (ok)
	{
		image.desc.debugName = debugName.c_str();
		handle = m_device->CreateTexture(image.desc, image.subresources);   // C++20: vector → std::span (포인터+개수 대신)
	}
	if (!handle.IsValid())
	{
		Log::Warn("텍스처 '%s' 를 쓸 수 없어 흰색으로 대체.", name.c_str());
		handle = m_white;
	}
	cache.emplace(name, handle);
	return handle;
}

TextureHandle TextureCache::LoadCube(const std::string& name, uint32_t* mipLevels)
{
	auto found = m_cubes.find(name);
	if (found != m_cubes.end())
	{
		if (mipLevels != nullptr) *mipLevels = found->second.mipLevels;
		return found->second.handle;
	}

	// 8비트 큐브맵은 "색" 텍스처이므로 sRGB 로 읽음. 부동소수점(HDR) DDS 는 TextureLoader 가
	// R16G16B16A16_FLOAT 로 읽고 라벨을 무시함.
	TextureImage image;
	bool ok = false;
	if (name == "builtin:black")
	{
		ok = TextureLoader::CreateCube(1, [](const float[3], uint8_t rgba[4]) { rgba[0] = rgba[1] = rgba[2] = 0; rgba[3] = 255; }, false, false, image);
	}
	else if (name == "builtin:sky")
	{
		// 절차적 하늘: 방향의 높이(y)만 봄. 지평선(y = 0)의 옅은 하늘색에서 천정(y = 1)의 짙은 파랑으로,
		// 지평선 아래는 금방 지면색이 됨. 값은 sRGB 바이트 — 감마 공간에서 섞어도 눈에 띄는 차이는 없고, 밉
		// 생성만 선형에서 함(TextureLoader 의 TEX_FILTER_SRGB).
		ok = TextureLoader::CreateCube(128, [](const float d[3], uint8_t rgba[4])
		{
			static constexpr float zenith[3] = { 52.0f, 105.0f, 185.0f };
			static constexpr float horizon[3] = { 185.0f, 208.0f, 230.0f };
			static constexpr float ground[3] = { 92.0f, 88.0f, 82.0f };
			const float y = d[1];
			const float* target = y >= 0.0f ? zenith : ground;
			const float t = y >= 0.0f ? powf(y, 0.6f) : (std::min)(1.0f, -y * 4.0f);
			for (int i = 0; i < 3; ++i) rgba[i] = static_cast<uint8_t>(horizon[i] + (target[i] - horizon[i]) * t + 0.5f);
			rgba[3] = 255;
		}, true, true, image);
	}
	else if (name == "builtin:skyfaces")
	{
		// 방향 검증용: 주축(절댓값이 가장 큰 성분)으로 면을 고르고 면마다 색을 다르게 칠함 — +X 빨강, −X
		// 청록, +Y 초록, −Y 자홍, +Z 파랑, −Z 노랑 (마주 보는 면은 보색). 면을 4×4 칸으로 나누는 격자선이 면
		// 경계에서 끊김 없이 이어지면 TextureLoader 의 텍셀 → 방향 변환이 맞는 것.
		ok = TextureLoader::CreateCube(64, [](const float d[3], uint8_t rgba[4])
		{
			static constexpr uint8_t colors[6][3] = { { 220, 50, 50 }, { 50, 200, 200 }, { 60, 200, 60 }, { 200, 60, 200 }, { 60, 90, 230 }, { 230, 210, 50 } };
			const float ax = fabsf(d[0]), ay = fabsf(d[1]), az = fabsf(d[2]);
			int face;
			float u, v;   // 면 위의 나머지 두 성분 ÷ 주축 → [−1, 1]
			if (ax >= ay && ax >= az) { face = d[0] > 0.0f ? 0 : 1; u = d[1] / ax; v = d[2] / ax; }
			else if (ay >= az)        { face = d[1] > 0.0f ? 2 : 3; u = d[0] / ay; v = d[2] / ay; }
			else                      { face = d[2] > 0.0f ? 4 : 5; u = d[0] / az; v = d[1] / az; }
			const bool line = fabsf(u * 2.0f - roundf(u * 2.0f)) < 0.04f || fabsf(v * 2.0f - roundf(v * 2.0f)) < 0.04f;
			for (int i = 0; i < 3; ++i) rgba[i] = line ? static_cast<uint8_t>(colors[face][i] / 3) : colors[face][i];
			rgba[3] = 255;
		}, true, true, image);
	}
	else
	{
		// 2D 텍스처와 같은 경로 규칙: "asset:<Assets 기준>" 또는 Textures\ 기준. 파일이면 큐브 DDS, 폴더면 면
		// 이미지 6장.
		static const char* kAssetPrefix = "asset:";
		const std::wstring path = name.rfind(kAssetPrefix, 0) == 0
			? Paths::GetAssetPath(ToWide(name.substr(6)).c_str())
			: Paths::GetAssetPath((L"Textures\\" + ToWide(name)).c_str());
		ok = TextureLoader::LoadCubeFromFile(path, true, true, image);
	}

	CubeEntry entry;
	if (ok)
	{
		image.desc.debugName = name.c_str();
		entry.handle = m_device->CreateTexture(image.desc, image.subresources);
		entry.mipLevels = image.desc.mipLevels;
	}
	if (!entry.handle.IsValid())
	{
		Log::Warn("큐브맵 '%s' 를 쓸 수 없어 검은 큐브로 대체.", name.c_str());
		entry.handle = m_blackCube;   // 검은 큐브 자신을 만들다 실패했다면 빈 핸들 → Initialize 가 실패함
		entry.mipLevels = 1;
	}
	m_cubes.emplace(name, entry);
	if (mipLevels != nullptr) *mipLevels = entry.mipLevels;
	return entry.handle;
}

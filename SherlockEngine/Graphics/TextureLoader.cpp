#include "pch.h"
#include "Graphics/TextureLoader.h"
#include "Core/Log.h"
#include <DirectXTex.h>
#include <algorithm>
#include <cstring>

namespace
{
	// DirectXTex ScratchImage(밉 체인) → 우리 TextureImage. 픽셀을 한 버퍼로 복사함.
	bool FromScratchImage(const DirectX::ScratchImage& scratch, Format format, TextureImage& out)
	{
		const DirectX::TexMetadata& meta = scratch.GetMetadata();
		out.desc = TextureDesc{};
		out.desc.width = static_cast<uint32_t>(meta.width);
		out.desc.height = static_cast<uint32_t>(meta.height);
		out.desc.format = format;
		out.desc.mipLevels = static_cast<uint32_t>(meta.mipLevels);
		out.desc.bindFlags = TextureBind_ShaderResource;

		out.pixels.clear();
		out.subresources.clear();

		size_t total = 0;
		for (size_t mip = 0; mip < meta.mipLevels; ++mip)
		{
			const DirectX::Image* image = scratch.GetImage(mip, 0, 0);
			if (image == nullptr) return false;
			total += image->slicePitch;
		}
		out.pixels.resize(total);

		size_t offset = 0;
		for (size_t mip = 0; mip < meta.mipLevels; ++mip)
		{
			const DirectX::Image* image = scratch.GetImage(mip, 0, 0);
			std::memcpy(out.pixels.data() + offset, image->pixels, image->slicePitch);
			TextureSubresource sub;
			sub.data = out.pixels.data() + offset;
			sub.rowPitch = static_cast<uint32_t>(image->rowPitch);
			out.subresources.push_back(sub);
			offset += image->slicePitch;
		}
		return true;
	}

	bool FinishImage(DirectX::ScratchImage& base, bool srgb, bool generateMips, TextureImage& out)
	{
		// 픽셀 바이트는 파일에 든 그대로(sRGB 인코딩된 값)임. *_SRGB 포맷은 "이 바이트를 샘플할 때
		// 선형으로 풀어라"는 라벨일 뿐 값을 바꾸지 않음. 그래서 여기서는 색공간 변환 없이
		// R8G8B8A8_UNORM으로 통일만 하고, 라벨(desc.format)만 sRGB로 붙임.
		//
		// 함정: DirectXTex::Convert(UNORM → UNORM_SRGB)는 입력을 선형으로 보고 sRGB로 "인코딩"함
		// (TEX_FILTER_SRGB_OUT 암시). 그 결과 128 회색이 188이 되어 화면에서 188로 읽혔음. 반대로 DDS가
		// 이미 *_SRGB 포맷이면 Convert가 "디코딩"하므로, 먼저 OverrideFormat으로 라벨을 떼어 냄.
		if (DirectX::IsSRGB(base.GetMetadata().format))
		{
			base.OverrideFormat(DirectX::MakeLinear(base.GetMetadata().format));
		}

		const DXGI_FORMAT target = DXGI_FORMAT_R8G8B8A8_UNORM;
		DirectX::ScratchImage converted;
		const DirectX::ScratchImage* current = &base;
		if (base.GetMetadata().format != target)
		{
			const HRESULT hr = DirectX::Convert(base.GetImages(), base.GetImageCount(), base.GetMetadata(),
				target, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted);
			if (FAILED(hr))
			{
				Log::Error("TextureLoader : 포맷 변환 실패. %s", Log::HrToString(hr).c_str());
				return false;
			}
			current = &converted;
		}

		DirectX::ScratchImage mipped;
		if (generateMips && current->GetMetadata().mipLevels == 1 && (current->GetMetadata().width > 1 || current->GetMetadata().height > 1))
		{
			// 박스 필터로 1×1까지. sRGB 데이터면 TEX_FILTER_SRGB(= SRGB_IN | SRGB_OUT): 풀어서 선형에서
			// 평균 내고 다시 인코딩함. 감마 공간에서 그대로 평균 내면 밉이 어두워짐
			// (흰 230 + 검 40 의 절반은 선형에서 ≈ 170, 감마에서 135).
			const DirectX::TEX_FILTER_FLAGS filter = srgb ? DirectX::TEX_FILTER_SRGB : DirectX::TEX_FILTER_DEFAULT;
			const HRESULT hr = DirectX::GenerateMipMaps(current->GetImages(), current->GetImageCount(), current->GetMetadata(),
				filter, 0, mipped);
			if (FAILED(hr))
			{
				Log::Error("TextureLoader : 밉맵 생성 실패. %s", Log::HrToString(hr).c_str());
				return false;
			}
			current = &mipped;
		}

		return FromScratchImage(*current, srgb ? Format::R8G8B8A8_UNORM_SRGB : Format::R8G8B8A8_UNORM, out);
	}
}

namespace
{
	// 파일 → ScratchImage. 확장자가 .dds 면 DDS, 아니면 WIC. LoadFromFile 과 LoadThumbnail 이 함께 씀.
	bool LoadScratchFromFile(const std::wstring& path, DirectX::ScratchImage& image)
	{
		DirectX::TexMetadata meta = {};
		HRESULT hr;

		const size_t dot = path.find_last_of(L'.');
		std::wstring ext = (dot == std::wstring::npos) ? L"" : path.substr(dot);
		std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

		if (ext == L".dds")
		{
			hr = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &meta, image);
		}
		else
		{
			// WIC: PNG, JPG, BMP, TIFF, GIF. 알파 없는 파일도 RGBA로 확장함.
			// IGNORE_SRGB: 파일의 색공간 메타데이터를 보고 포맷을 *_SRGB로 바꾸지 않게 함. 라벨은 우리가 붙임.
			hr = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_FORCE_RGB | DirectX::WIC_FLAGS_IGNORE_SRGB, &meta, image);
		}
		if (FAILED(hr))
		{
			Log::Error("TextureLoader : 파일 로드 실패 %s. %s", Log::ToUtf8(path.c_str()).c_str(), Log::HrToString(hr).c_str());
			return false;
		}
		return true;
	}
}

bool TextureLoader::LoadFromFile(const std::wstring& path, bool srgb, bool generateMips, TextureImage& out)
{
	DirectX::ScratchImage image;
	if (!LoadScratchFromFile(path, image)) return false;

	if (!FinishImage(image, srgb, generateMips, out))
	{
		return false;
	}
	Log::Info("텍스처 로드: %s (%ux%u, 밉 %u, %s)", Log::ToUtf8(path.c_str()).c_str(),
		out.desc.width, out.desc.height, out.desc.mipLevels, srgb ? "sRGB" : "linear");
	return true;
}

bool TextureLoader::CreateChecker(uint32_t size, uint32_t cellsPerSide, const uint8_t colorA[4], const uint8_t colorB[4],
	bool srgb, bool generateMips, TextureImage& out)
{
	DirectX::ScratchImage image;
	if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 1)))
	{
		return false;
	}
	const DirectX::Image* img = image.GetImage(0, 0, 0);
	const uint32_t cell = size / (cellsPerSide > 0 ? cellsPerSide : 1);
	for (uint32_t y = 0; y < size; ++y)
	{
		uint8_t* row = img->pixels + y * img->rowPitch;
		for (uint32_t x = 0; x < size; ++x)
		{
			const bool even = (((x / cell) + (y / cell)) % 2) == 0;
			const uint8_t* c = even ? colorA : colorB;
			std::memcpy(row + x * 4, c, 4);
		}
	}
	return FinishImage(image, srgb, generateMips, out);
}

bool TextureLoader::CreateSolid(uint32_t size, const uint8_t rgba[4], bool srgb, TextureImage& out)
{
	DirectX::ScratchImage image;
	if (FAILED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 1)))
	{
		return false;
	}
	const DirectX::Image* img = image.GetImage(0, 0, 0);
	for (uint32_t y = 0; y < size; ++y)
	{
		uint8_t* row = img->pixels + y * img->rowPitch;
		for (uint32_t x = 0; x < size; ++x)
		{
			std::memcpy(row + x * 4, rgba, 4);
		}
	}
	return FinishImage(image, srgb, false, out);
}

bool TextureLoader::LoadThumbnail(const std::wstring& path, uint32_t maxSize, TextureImage& out)
{
	DirectX::ScratchImage image;
	if (!LoadScratchFromFile(path, image)) return false;

	// 블록 압축(BC1~7)은 Resize/Convert 가 다루지 못하므로 먼저 압축을 풂.
	DirectX::ScratchImage decompressed;
	const DirectX::ScratchImage* current = &image;
	if (DirectX::IsCompressed(image.GetMetadata().format))
	{
		const HRESULT hr = DirectX::Decompress(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DXGI_FORMAT_R8G8B8A8_UNORM, decompressed);
		if (FAILED(hr))
		{
			Log::Warn("TextureLoader : 썸네일용 압축 해제 실패 %s. %s", Log::ToUtf8(path.c_str()).c_str(), Log::HrToString(hr).c_str());
			return false;
		}
		current = &decompressed;
	}

	// 밉 0 만 씀. 긴 변을 maxSize 에 맞춤 (비율 유지).
	const DirectX::Image* base = current->GetImage(0, 0, 0);
	if (base == nullptr) return false;
	DirectX::ScratchImage resized;
	const size_t longest = (std::max)(base->width, base->height);
	if (maxSize > 0 && longest > maxSize)
	{
		const size_t w = (std::max<size_t>)(1, base->width * maxSize / longest);
		const size_t h = (std::max<size_t>)(1, base->height * maxSize / longest);
		const HRESULT hr = DirectX::Resize(*base, w, h, DirectX::TEX_FILTER_DEFAULT, resized);
		if (FAILED(hr))
		{
			Log::Warn("TextureLoader : 썸네일 축소 실패 %s. %s", Log::ToUtf8(path.c_str()).c_str(), Log::HrToString(hr).c_str());
			return false;
		}
		base = resized.GetImage(0, 0, 0);
	}

	// 밉 0 하나짜리 ScratchImage 로 옮겨 FinishImage 의 포맷 통일을 거침. srgb=false: UNORM 라벨 (헤더 주석).
	DirectX::ScratchImage single;
	if (FAILED(single.InitializeFromImage(*base))) return false;
	return FinishImage(single, false, false, out);
}

bool TextureLoader::LoadFromMemory(const uint8_t* data, size_t size, bool srgb, bool generateMips, TextureImage& out)
{
	if (data == nullptr || size == 0) return false;
	DirectX::ScratchImage image;
	DirectX::TexMetadata meta = {};

	// DDS 매직("DDS ") 이면 DDS, 아니면 WIC (PNG/JPG). 모델 파일이 확장자를 알려 주지 않을 수 있어 내용으로 판단함.
	HRESULT hr;
	if (size >= 4 && data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ')
	{
		hr = DirectX::LoadFromDDSMemory(data, size, DirectX::DDS_FLAGS_NONE, &meta, image);
	}
	else
	{
		hr = DirectX::LoadFromWICMemory(data, size, DirectX::WIC_FLAGS_FORCE_RGB | DirectX::WIC_FLAGS_IGNORE_SRGB, &meta, image);
	}
	if (FAILED(hr))
	{
		Log::Error("TextureLoader : 메모리 이미지 디코딩 실패 (%zu 바이트). %s", size, Log::HrToString(hr).c_str());
		return false;
	}
	return FinishImage(image, srgb, generateMips, out);
}

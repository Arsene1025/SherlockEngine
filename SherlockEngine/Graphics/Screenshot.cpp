#include "pch.h"
#include "Graphics/Screenshot.h"
#include "Core/Log.h"
#include <DirectXTex.h>

bool Screenshot::SavePng(const std::wstring& path, const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0 || rgba.size() < static_cast<size_t>(width) * height * 4)
	{
		Log::Error("Screenshot : 픽셀 데이터가 비어 있음 (%ux%u, %zu 바이트).", width, height, rgba.size());
		return false;
	}
	DirectX::Image image = {};
	image.width = width;
	image.height = height;
	image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	image.rowPitch = static_cast<size_t>(width) * 4;
	image.slicePitch = image.rowPitch * height;
	image.pixels = const_cast<uint8_t*>(rgba.data());

	const size_t slash = path.find_last_of(L"\\/");
	if (slash != std::wstring::npos) CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);

	// 알파는 1 로 저장한다 (백버퍼 알파는 의미가 없다).
	std::vector<uint8_t> opaque(rgba.begin(), rgba.begin() + image.slicePitch);
	for (size_t i = 3; i < opaque.size(); i += 4) opaque[i] = 255;
	image.pixels = opaque.data();

	const HRESULT hr = DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path.c_str());
	if (FAILED(hr))
	{
		Log::Error("Screenshot : PNG 저장 실패 (%s). %s", Log::ToUtf8(path.c_str()).c_str(), Log::HrToString(hr).c_str());
		return false;
	}
	Log::Info("스크린샷 저장: %s (%ux%u)", Log::ToUtf8(path.c_str()).c_str(), width, height);
	return true;
}

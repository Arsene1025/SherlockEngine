#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "RHI/ResourceDesc.h"

// 텍스처 파일 → CPU 이미지(밉 체인 포함). API 중립.
//
// 디코딩과 밉맵 생성은 DirectXTex(vcpkg)가 한다. WIC(PNG·JPG·BMP)와 DDS를 읽는다.
// 결과는 TextureDesc + 밉 레벨별 TextureSubresource 배열이므로 Device::CreateTexture에
// 그대로 넘긴다. D3D 헤더는 쓰지 않는다 — DirectXTex의 D3D11 헬퍼(CreateTexture 등)는
// 일부러 쓰지 않는다. 그것을 쓰면 로더가 D3D11 폴더에 들어가야 한다.
//
// sRGB: 색 텍스처(알베도)는 srgb=true 로 읽어 *_UNORM_SRGB 포맷으로 만든다. 샘플할 때
// 하드웨어가 선형으로 풀고, 조명은 선형에서 계산되며, 백버퍼 sRGB 뷰가 다시 인코딩한다.
// 노멀맵·마스크 같은 "숫자" 텍스처는 srgb=false.
class TextureImage
{
public:
	TextureDesc desc;                            // format, width, height, mipLevels
	std::vector<uint8_t> pixels;                 // 모든 밉을 이어 붙인 버퍼
	std::vector<TextureSubresource> subresources;   // 밉마다 pixels 안의 포인터와 rowPitch

	bool IsValid() const { return desc.width > 0 && !subresources.empty(); }
};

namespace TextureLoader
{
	// 파일에서 읽는다. generateMips면 1×1까지 밉 체인을 만든다.
	bool LoadFromFile(const std::wstring& path, bool srgb, bool generateMips, TextureImage& out);

	// 9단계: 메모리의 파일 내용(PNG/JPG/DDS 바이트)에서 읽는다. 모델에 내장된 이미지용.
	bool LoadFromMemory(const uint8_t* data, size_t size, bool srgb, bool generateMips, TextureImage& out);

	// 절차적 텍스처. 파일 없이 검증할 때 쓴다.
	// 체커: cellsPerSide×cellsPerSide 격자, 두 색 교대.
	bool CreateChecker(uint32_t size, uint32_t cellsPerSide, const uint8_t colorA[4], const uint8_t colorB[4],
		bool srgb, bool generateMips, TextureImage& out);
	// 단색 (예: 1×1 흰색 = "텍스처 없음"의 기본값, 128 회색 = 감마 검증).
	bool CreateSolid(uint32_t size, const uint8_t rgba[4], bool srgb, TextureImage& out);
}

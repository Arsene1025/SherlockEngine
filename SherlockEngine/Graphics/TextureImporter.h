#pragma once
#include <cstdint>
#include <string>

// 텍스처 임포트 설정과 변환 캐시.
//
// 원본 PNG/JPG 는 그대로 두고, 읽을 때 "BC7 압축 + 밉 체인 + (선택) 해상도 축소" DDS 를 한 번 만들어
// <프로젝트>\Library\TextureCache\<해시>.dds 에 둠. 다음부터는 그 DDS 를 읽음 (TextureLoader 는 BC 를 압축 그대로 GPU 에 올림).
// 변환은 에디터만 함 (SetBuildEnabled). 게임 빌드는 캐시가 있으면 쓰고 없으면 원본을 그대로 읽음.
//
// 설정은 텍스처 이름 단위 (재질이 저장하는 이름 그대로 — 모델 내장 이미지도 같음). 프로젝트 Assets\TextureSettings.json 에
// 기본값과 다른 것만 저장함. 기본값 = 압축 켬, 밉 전체, 해상도 그대로. 작은 텍스처(256 미만)는 기본값일 때 변환하지 않음.
struct TextureImportSettings
{
	bool compress = true;       // BC7 로 압축. 끄면 R8G8B8A8 로 저장 (밉·크기 설정만 적용)
	uint32_t mipLevels = 0;     // 0 = 1×1 까지 전체, 1 = 밉 없음, N = 상위 N 단계
	uint32_t maxSize = 0;       // 0 = 원본 크기, 아니면 긴 변이 이 값 이하가 되도록 절반씩 줄임 (2 의 거듭제곱 비율)

	bool IsDefault() const { return compress && mipLevels == 0 && maxSize == 0; }
	bool operator==(const TextureImportSettings& other) const { return compress == other.compress && mipLevels == other.mipLevels && maxSize == other.maxSize; }
};

namespace TextureImporter
{
	// 에디터가 켬. 꺼져 있으면 캐시 조회만 함 (변환하지 않음).
	void SetBuildEnabled(bool enabled);
	bool IsBuildEnabled();

	// 텍스처 이름의 설정. 없으면 기본값. 프로젝트(Assets 루트)가 바뀌면 파일을 다시 읽음.
	TextureImportSettings GetSettings(const std::string& name);
	// 설정을 바꾸고 TextureSettings.json 에 저장함. 기본값이면 항목을 지움. 캐시 파일은 해시가 달라져 자동으로 새로 만들어짐.
	void SetSettings(const std::string& name, const TextureImportSettings& settings);

	// 원본 바이트(PNG/JPG)에 대응하는 변환 캐시 DDS 경로. 변환이 필요 없거나(설정이 기본값이고 작은 텍스처, 이미 DDS, 변환 불가),
	// 캐시가 없고 변환도 못 하면 빈 문자열 — 호출자가 원본을 그대로 읽음.
	// 캐시에 있으면 경로를 돌려주고, 없으면 (에디터라면) 만들어서 돌려줌. 만들 때는 수 초 걸릴 수 있음 (4096² 한 장 약 3초).
	std::wstring ResolveCached(const std::string& name, const uint8_t* data, size_t size, bool srgb);

	// 캐시 폴더 (끝에 백슬래시 포함). 프로젝트가 있으면 <프로젝트>\Library\TextureCache\, 없으면 <exe>\TextureCache\.
	std::wstring GetCacheDir();
}

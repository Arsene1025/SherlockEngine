#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 텍스처 임포트 설정과 변환 캐시.
//
// 원본 PNG/JPG 는 그대로 두고, 읽을 때 "BC7 압축 + 밉 체인 + (선택) 해상도 축소" DDS 를 한 번 만들어
// <프로젝트>\Library\TextureCache\<해시>.dds 에 둠. 다음부터는 그 DDS 를 읽음 (TextureLoader 는 BC 를 압축 그대로 GPU 에 올림).
// 변환은 에디터만 함 (SetBuildEnabled).
//
// 게임 패키지(GameBuilder): 빌드할 때 패키지가 쓰는 텍스처를 모두 변환(CookForBuild)하고, 쓰이는 DDS 와
// "텍스처 이름 + 색공간 → DDS 파일" 목록(TextureCache\manifest.json)만 싣고 원본 PNG/JPG 는 뺌. 게임은 목록을 보고
// DDS 를 바로 읽어서 원본을 읽거나 해시를 계산하지 않음 (FindCooked). 목록에 없는 텍스처는 위의 해시 캐시 → 원본 순으로 읽음.
//
// 설정은 텍스처 이름 단위 (재질이 저장하는 이름 그대로 — 모델 내장 이미지도 같음). 프로젝트 Assets\TextureSettings.json 에
// 기본값과 다른 것만 저장함. 기본값 = 압축 켬, 밉 전체, 해상도 그대로. 작은 텍스처(256 미만)는 기본값일 때 변환하지 않음.
struct TextureImportSettings
{
	bool compress = true;     // BC7 로 압축. 끄면 R8G8B8A8 로 저장 (밉·크기 설정만 적용)
	uint32_t mipLevels = 0;   // 0 = 1×1 까지 전체, 1 = 밉 없음, N = 상위 N 단계
	uint32_t maxSize = 0;     // 0 = 원본 크기, 아니면 긴 변이 이 값 이하가 되도록 절반씩 줄임 (2 의 거듭제곱 비율)

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

	// 게임 빌드용 변환. ResolveCached 와 같지만 기본 설정의 작은 텍스처(256 미만)도 R8G8B8A8 + 밉 체인 DDS 로 만듦 — 원본을
	// 읽을 때 TextureLoader 가 만드는 것과 같은 결과라 화면은 그대로. 빈 문자열이면 변환할 수 없는 것 (이미 DDS, 디코딩 실패) — 원본을 실어야 함.
	std::wstring CookForBuild(const std::string& name, const uint8_t* data, size_t size, bool srgb);

	// 캐시 폴더 (끝에 백슬래시 포함). 엔진 소스 트리 안의 프로젝트면 <프로젝트>\Library\TextureCache\, 아니면(배포 포함) <exe>\TextureCache\.
	std::wstring GetCacheDir();

	// ---- 게임 패키지의 변환 목록 (캐시 폴더의 manifest.json) ----
	struct CookedTexture
	{
		std::string name;   // 재질이 쓰는 텍스처 이름
		bool srgb = false;
		std::wstring file;   // 캐시 폴더 안의 파일 이름 (<해시>.dds)
	};
	// 목록을 dir\manifest.json 에 씀 (DDS 파일 복사는 호출자가 함).
	bool WriteManifest(const std::wstring& dir, const std::vector<CookedTexture>& textures, std::string& error);
	// 게임(SetBuildEnabled 가 꺼진 쪽)에서 이름 + 색공간의 변환된 DDS 경로. 목록이 없거나 이름이 없으면 빈 문자열.
	// 요청한 색공간이 없고 다른 쪽만 있으면 그것을 돌려줌 (경고 남김 — TextureLoader 가 라벨을 다시 붙이므로 흰색보다 나음).
	std::wstring FindCooked(const std::string& name, bool srgb);
	// 이름이 목록에 있는지 (색공간 무관). GltfImporter 가 원본 이미지 바이트를 읽지 않고 건너뛸 때 씀.
	bool HasCooked(const std::string& name);
}

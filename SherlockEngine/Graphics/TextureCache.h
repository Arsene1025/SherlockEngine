#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include "RHI/Handle.h"

namespace RHI { class Device; }
class Scene;

// 이름 → 텍스처 핸들 캐시. Renderer 에서 떼어 낸 것 — 텍스처를 읽고 만들고 버리는 일은 "프레임을 그리는
// 일"과 수명도 책임도 다름 (RHI 다음 단계에서 비동기 로딩·스트리밍이 붙을 자리).
//
// 캐시는 둘:
//   2D    키 = (이름, sRGB 여부). 같은 파일도 색공간이 다르면 다른 텍스처임 (알베도 sRGB, 노멀 선형)
//   큐브  키 = 이름. 스카이박스·IBL 공용. 실패 시 대체물이 흰 2D 가 아니라 검은 큐브여야 해서 따로 둠
// 실패한 이름도 대체 핸들(흰색 / 검은 큐브)로 캐시에 남으므로 프레임마다 다시 읽지 않음.
class TextureCache
{
public:
	// 내장 텍스처(흰색, 평평한 노멀, 검은 큐브)를 만듦. 하나라도 실패하면 false.
	bool Initialize(RHI::Device* device);
	void Shutdown();

	// 재질의 텍스처 이름 → 핸들. "builtin:*"은 절차적으로 만들고, 씬에 같은 이름의 이미지가 있으면 그 바이트에서,
	// 없으면 Assets/Textures/ 의 파일에서 ("asset:" 이면 Assets/ 기준) 읽음. 빈 이름과 실패는 흰색 텍스처.
	TextureHandle Load2D(const std::string& name, bool srgb, const Scene* scene);
	// 스카이박스·IBL 이름 → 큐브 텍스처 핸들. "builtin:*" 은 절차적으로 만들고, 그 밖에는 Assets/Textures/
	// 기준 파일(큐브 DDS) 또는 폴더(면 이미지 6장), "asset:" 이면 Assets/ 기준. 실패하면 1×1 검은 큐브.
	// 항상 sRGB 로 읽음. mipLevels 가 있으면 그 큐브의 밉 수를 돌려줌 (반사 프리필터의 거칠기 → 밉 매핑에 씀).
	TextureHandle LoadCube(const std::string& name, uint32_t* mipLevels = nullptr);

	TextureHandle GetWhite() const { return m_white; }             // "텍스처 없음"의 기본값
	TextureHandle GetFlatNormal() const { return m_flatNormal; }   // 탄젠트 공간 (0, 0, 1)
	TextureHandle GetBlackCube() const { return m_blackCube; }     // 환경이 없을 때 t9~t11 에 꽂는 1×1 큐브

	// 내장("builtin:")이 아닌 텍스처를 모두 파괴하고 캐시에서 지움 (씬을 바꿀 때). 세대 번호가 올라감.
	void ReleaseLoaded();
	// ReleaseLoaded 가 부를 때마다 1 증가. 핸들을 기억해 둔 쪽(재질 셋)이 "내가 본 뒤로 캐시가 비워졌는가"를
	// 이 값 하나로 판단함.
	uint64_t GetGeneration() const { return m_generation; }
	size_t GetCount() const { return m_2d[0].size() + m_2d[1].size() + m_cubes.size(); }

private:
	struct CubeEntry
	{
		TextureHandle handle;
		uint32_t mipLevels = 1;
	};

	RHI::Device* m_device = nullptr;
	TextureHandle m_white;
	TextureHandle m_flatNormal;
	TextureHandle m_blackCube;
	std::unordered_map<std::string, TextureHandle> m_2d[2];   // [0] 선형, [1] sRGB. 키 = 이름 (조회할 때 문자열을 새로 만들지 않음)
	std::unordered_map<std::string, CubeEntry> m_cubes;
	uint64_t m_generation = 0;
};

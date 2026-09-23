#pragma once
#include <string>
#include <DirectXMath.h>

// 재질(CPU 데이터). GPU 쪽(상수버퍼 핸들, 텍스처·샘플러 핸들, ResourceSet, PSO)은
// Renderer의 캐시가 갖는다. Mesh ↔ GpuMesh 와 같은 경계다 (rendering-analysis D3).
//
// 텍스처는 핸들이 아니라 "이름"으로 참조한다. Scene은 Device를 모르므로 파일을 열 수 없고,
// Renderer의 텍스처 캐시가 이름 → TextureHandle 을 맡는다.
//   - 파일 경로: Assets/Textures/ 기준 상대 경로 ("uv_checker.png")
//   - 내장:      "builtin:white", "builtin:checker", "builtin:gray128", "builtin:flatnormal"
//   - 빈 문자열: 텍스처 없음 (알베도는 1×1 흰색, 노멀 맵은 평평한 노멀로 대체)

enum class SamplerPreset : uint8_t
{
	LinearWrap,       // 3선형 + 반복. 기본값
	LinearClamp,
	AnisotropicWrap,  // 비등방 16x. 바닥처럼 비스듬히 보는 면에 필요
	PointWrap,        // 최근접(픽셀 아트). 밉맵은 씀
	PointNoMip,       // 최근접 + 밉맵 끔. "밉맵이 없으면 어떻게 되나"의 대조군
};

struct Material
{
	std::string name;

	DirectX::XMFLOAT4 baseColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);   // 정점 색·텍스처에 곱해진다
	DirectX::XMFLOAT3 specularColor = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
	float shininess = 32.0f;

	// 5단계
	std::string albedoTexture;             // 위 규칙의 이름. 비면 흰색
	bool albedoSrgb = true;                // 색 텍스처는 sRGB. 데이터 텍스처(노멀·마스크)는 false
	SamplerPreset sampler = SamplerPreset::LinearWrap;
	DirectX::XMFLOAT2 uvScale = DirectX::XMFLOAT2(1.0f, 1.0f);
	bool unlit = false;                    // 조명 없이 알베도 출력 (감마 검증)

	// 6단계: 노멀 맵. 항상 선형(sRGB 아님). 탄젠트 공간, +X = u 증가, +Y = v 증가(아래), +Z = 표면 밖.
	std::string normalTexture;             // 비면 평평한 노멀 (128,128,255)
	float normalStrength = 1.0f;           // xy 배율. 0 이면 노멀 맵을 무시한 것과 같다

	// 9단계: 알파 컷아웃 (glTF alphaMode MASK). 0 = 끔. 알베도 알파가 이 값보다 작은 픽셀은 버린다(clip).
	float alphaCutoff = 0.0f;

	// 파이프라인 상태에 영향을 주는 옵션. 값이 다르면 다른 PSO가 된다.
	bool wireframe = false;
	bool doubleSided = false;   // 뒷면 컬링 끔
};

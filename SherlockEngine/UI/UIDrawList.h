#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <DirectXMath.h>

// UI 그리기 목록. Scene 쪽(UISystem::Build)이 CPU 로 만들고 Renderer 가 읽기만 함 (GPU 자원은 Renderer 의 것 —
// docs/plan/ui-canvas-plan.html 3.1, 3.4).
//
// 메인 뷰와 미리보기 뷰의 정점·인덱스·캔버스 행렬을 한 배열에 이어 붙임. Dynamic 버퍼는 UpdateBuffer 가 프레임 슬롯을
// 통째로 바꾸므로 뷰마다 따로 올리면 D3D12 에서 이미 기록한 메인 패스의 정점을 덮어씀 — 그래서 한 목록·한 업로드로
// 묶고 패스마다 명령 구간만 고름.

// Shaders/UI.hlsli 의 UIVSInput 과 같은 배치 (40 B)
struct UIVertex
{
	DirectX::XMFLOAT2 position;   // 캔버스 좌표
	DirectX::XMFLOAT2 uv;
	uint32_t color;               // RGBA8, sRGB 값 (UNORM 뷰에 그대로 섞음 = 감마 공간 블렌딩)
	DirectX::XMFLOAT4 clip;       // 클립 사각형 (캔버스 좌표 minX, minY, maxX, maxY). 마스크가 없으면 아주 큰 사각형
	uint32_t canvas;              // 아래 16비트 = canvasMatrices 번호, 비트 16 = 글자(텍스처 R 을 알파로, 2단계)
};
static_assert(sizeof(UIVertex) == 40,"UIVertex 는 40 바이트 (UI.hlsli 와 같은 배치)");

constexpr uint32_t kUIVertexGlyphFlag = 1u << 16;
constexpr uint32_t kUIMaxCanvases = 32;   // UIConstants 의 행렬 배열 크기 (ShaderConstants.h)

// 글리프 아틀라스를 가리키는 명령의 텍스처 이름 (TextureCache 가 아니라 Renderer 의 아틀라스 텍스처)
constexpr const char* kUIGlyphAtlasTexture = "builtin:ui-glyphs";

// 글리프 아틀라스의 CPU 사본 (UI/GlyphAtlas 가 소유). Renderer 는 version 이 바뀌면 텍스처를 새로 만들어 전체를 올리고,
// 아니면 dirty 사각형만 UpdateTexture 로 올린 뒤 ClearDirty. 올리지 못한 프레임(그리기를 건너뜀)이 있어도 dirty 가 쌓여 있다가
// 다음에 올라감.
struct UIAtlasImage
{
	std::vector<uint8_t> pixels;   // R8, size × size
	uint32_t size = 0;
	uint64_t version = 0;
	uint32_t dirtyMinX = 0, dirtyMinY = 0, dirtyMaxX = 0, dirtyMaxY = 0;   // [min, max)

	bool HasDirty() const { return dirtyMaxX > dirtyMinX && dirtyMaxY > dirtyMinY; }
	void ClearDirty() { dirtyMinX = dirtyMinY = dirtyMaxX = dirtyMaxY = 0; }
	void MarkDirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
	{
		if (!HasDirty())
		{
			dirtyMinX = x;
			dirtyMinY = y;
			dirtyMaxX = x + w;
			dirtyMaxY = y + h;
			return;
		}
		dirtyMinX = x < dirtyMinX ? x : dirtyMinX;
		dirtyMinY = y < dirtyMinY ? y : dirtyMinY;
		dirtyMaxX = x + w > dirtyMaxX ? x + w : dirtyMaxX;
		dirtyMaxY = y + h > dirtyMaxY ? y + h : dirtyMaxY;
	}
};

struct UIDrawCmd
{
	std::string texture;   // TextureCache::Load2D 이름. 빈 문자열 = 흰색, kUIGlyphAtlasTexture = 글리프 아틀라스
	uint32_t indexStart = 0;
	uint32_t indexCount = 0;
};

struct UIViewRange
{
	uint32_t firstCommand = 0;
	uint32_t commandCount = 0;
	uint32_t worldCommandCount = 0;   // 앞쪽 이만큼은 World 캔버스 (깊이 검사 파이프라인), 나머지는 Overlay
	uint32_t width = 0;    // 이 목록을 만들 때 쓴 출력 크기 (Renderer 가 그릴 타깃과 맞는지 확인)
	uint32_t height = 0;
};

struct UIDrawList
{
	std::vector<UIVertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<UIDrawCmd> commands;
	UIViewRange views[2];                               // [0] 메인, [1] 카메라 미리보기
	std::vector<DirectX::XMFLOAT4X4> canvasMatrices;   // 캔버스 좌표 → 클립 공간. 행벡터 관례(mul(v, M)) 그대로, 올릴 때 전치
	UIAtlasImage* atlas = nullptr;                      // 글리프 아틀라스 (UISystem 소유, Clear 가 지우지 않음). Renderer 가 dirty 를 비움

	void Clear()
	{
		vertices.clear();
		indices.clear();
		commands.clear();
		views[0] = UIViewRange{};
		views[1] = UIViewRange{};
		canvasMatrices.clear();
	}
	bool IsEmpty() const { return commands.empty(); }
};

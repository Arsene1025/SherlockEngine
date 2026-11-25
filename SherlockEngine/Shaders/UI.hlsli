// 게임 UI 캔버스 (docs/plan/ui-canvas-plan.html). UIVertexShader.hlsl / UIPixelShader.hlsl 이 씀.
//
// 셋 0 = { b4 UIConstants, s0 }, 셋 1 = { t0 텍스처 } (ShaderConstants.h 의 kCBSlotUI 등). Common.hlsli 를 include 하지 않음 —
// UI 패스는 프레임 셋(b0)을 묶지 않음.
//
// 색은 sRGB 값 그대로 다룸: 텍스처는 UNORM(선형 변환 없음)으로 읽고, UNORM 렌더 타깃 뷰에 써서 감마 공간에서 섞음 (3.2).

#define MAX_UI_CANVASES 32
#define UI_GLYPH_FLAG 0x10000u

cbuffer UIConstants : register(b4)
{
    float4x4 canvasMatrices[MAX_UI_CANVASES];   // 캔버스 좌표 → 클립 (행벡터 관례, C++ 에서 전치해서 올림)
};

Texture2D uiTexture : register(t0);
SamplerState uiSampler : register(s0);

struct UIVSInput
{
    float2 position : POSITION;   // 캔버스 좌표
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;        // R8G8B8A8_UNORM
    float4 clip : TEXCOORD1;      // 클립 사각형 (캔버스 좌표 minX, minY, maxX, maxY)
    uint canvas : BLENDINDICES0;  // 아래 16비트 = 캔버스 번호, 비트 16 = 글자
};

struct UIPSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 clip : TEXCOORD1;
    float2 canvasPos : TEXCOORD2;
    nointerpolation uint flags : TEXCOORD3;
};

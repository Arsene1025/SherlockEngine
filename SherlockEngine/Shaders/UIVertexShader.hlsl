#include "UI.hlsli"

UIPSInput VS_UI(UIVSInput input)
{
    UIPSInput output;
    const uint canvas = min(input.canvas & 0xFFFFu, MAX_UI_CANVASES - 1);
    output.position = mul(float4(input.position, 0.0f, 1.0f), canvasMatrices[canvas]);
    output.uv = input.uv;
    output.color = input.color;
    output.clip = input.clip;
    output.canvasPos = input.position;
    output.flags = input.canvas & UI_GLYPH_FLAG;
    return output;
}

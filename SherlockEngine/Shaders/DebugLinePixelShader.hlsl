#include "Common.hlsli"

struct LinePSInput
{
    float4 position : SV_POSITION;
    float4 col : COLOR0;
};

// HDR 씬 텍스처에 선형 색 그대로 씀 (톤 패스가 노출·톤매핑을 적용함).
float4 PS_Line(LinePSInput input) : SV_TARGET
{
    return float4(input.col.rgb, 1.0f);
}

#include "Common.hlsli"

struct LinePSInput
{
    float4 position : SV_POSITION;
    float4 col : COLOR0;
};

float4 PS_Line(LinePSInput input) : SV_TARGET
{
    return float4(input.col.rgb, 1.0f);
}

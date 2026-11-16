#include "Common.hlsli"

struct LineVSInput
{
    float3 position : POSITION;
    float4 col : COLOR0;
};

struct LineVSOutput
{
    float4 position : SV_POSITION;
    float4 col : COLOR0;
};

LineVSOutput VS_Line(LineVSInput input)
{
    LineVSOutput output;
    output.position = mul(float4(input.position, 1.0f), viewProj);
    output.col = input.col;
    return output;
}

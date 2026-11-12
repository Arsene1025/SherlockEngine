#include "Common.hlsli"

// 디버그 선 (본 표시 등). 정점은 월드 공간 위치와 색을 가짐 — C++ 가 이미 월드로 옮겨 올림 (world 행렬 없음).
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

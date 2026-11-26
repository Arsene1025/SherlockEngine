// 알파 테스트 그림자 VS (ShadowAlpha.hlsli). ShadowVertexShader 와 같은 변환에 정점 색·UV 를 더 넘김.
// 입력 레이아웃은 메인 패스와 같은 VERTEX(64B).
#include "ShadowAlpha.hlsli"

struct ShadowAlphaVSInput
{
    float3 position : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
};

ShadowAlphaVSOutput VS_ShadowAlpha(ShadowAlphaVSInput input)
{
    ShadowAlphaVSOutput o;
    const float4 worldPosition = mul(float4(input.position, 1.0f), world);
    o.position = mul(worldPosition, shadowViewProj);   // b5: 지금 그리는 그림자 뷰
    o.col = input.col;
    o.uv = input.uv * uvScale;
    return o;
}

// 스키닝 메시용 알파 테스트 그림자 VS (ShadowAlpha.hlsli). ShadowSkinVertexShader 와 같은 스킨 변환에 정점 색·UV 를 더 넘김.
// 입력 레이아웃은 스킨 메인 패스와 같은 VERTEX_SKINNED(84B).
#include "ShadowAlpha.hlsli"

struct ShadowAlphaSkinVSInput
{
    float3 position : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    uint4 jointIndices : BLENDINDICES;
    float4 jointWeights : BLENDWEIGHT;
};

ShadowAlphaVSOutput VS_ShadowAlphaSkin(ShadowAlphaSkinVSInput input)
{
    ShadowAlphaVSOutput o;
    const float4 skinnedPosition = mul(float4(input.position, 1.0f), SkinMatrix(input.jointIndices, input.jointWeights));
    const float4 worldPosition = mul(skinnedPosition, world);
    o.position = mul(worldPosition, shadowViewProj);   // b5: 지금 그리는 그림자 뷰
    o.col = input.col;
    o.uv = input.uv * uvScale;
    return o;
}

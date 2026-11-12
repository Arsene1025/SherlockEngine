#include "Common.hlsli"

// 스키닝 메시용 그림자 맵 패스 VS. 위치만 스킨해서 내보냄 (ShadowVertexShader 와 같은 변환).
// 입력 레이아웃은 스킨 메인 패스와 같은 VERTEX_SKINNED(84B)를 씀.
struct ShadowSkinVSInput
{
    float3 position : POSITION;
    uint4 jointIndices : BLENDINDICES;
    float4 jointWeights : BLENDWEIGHT;
};

float4 VS_ShadowSkin(ShadowSkinVSInput input) : SV_POSITION
{
    const float4 skinnedPosition = mul(float4(input.position, 1.0f), SkinMatrix(input.jointIndices, input.jointWeights));
    const float4 worldPosition = mul(skinnedPosition, world);
    return mul(worldPosition, shadowViewProj);   // b5: 지금 그리는 그림자 뷰
}

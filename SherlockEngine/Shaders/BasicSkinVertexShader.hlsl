#include "Common.hlsli"

// 스키닝 메시용 메인 패스 VS. BasicVertexShader 와 같은 VSOutput 을 내므로 픽셀 셰이더는 그대로 씀.
// 입력은 VSInput 뒤에 조인트 번호(uint4)와 가중치(float4)가 붙음 (VERTEX_SKINNED 84바이트).
struct SkinVSInput
{
    float3 position : POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;
    uint4 jointIndices : BLENDINDICES;
    float4 jointWeights : BLENDWEIGHT;
};

VSOutput VS_MainSkin(SkinVSInput vsInput)
{
    VSOutput vsOutput = (VSOutput)0;

    // 오브젝트 로컬(메시 노드 공간)에서 스킨한 뒤 월드로. 팔레트는 이미 메시 노드 변환을 상쇄해 둠 (Skin 상수버퍼 참고).
    const matrix skin = SkinMatrix(vsInput.jointIndices, vsInput.jointWeights);
    const float4 skinnedPosition = mul(float4(vsInput.position, 1.0f), skin);
    const float3 skinnedNormal = mul(float4(vsInput.normal, 0.0f), skin).xyz;
    const float3 skinnedTangent = mul(float4(vsInput.tangent.xyz, 0.0f), skin).xyz;

    const float4 worldPosition = mul(skinnedPosition, world);

    vsOutput.position = mul(worldPosition, viewProj);
    vsOutput.worldPosition = worldPosition.xyz;
    vsOutput.col = vsInput.col;
    vsOutput.uv = vsInput.uv * uvScale;
    vsOutput.normal = normalize(mul(float4(skinnedNormal, 0.0f), worldInvTranspose).xyz);
    vsOutput.tangent = float4(normalize(mul(float4(skinnedTangent, 0.0f), world).xyz), TangentHandedness(vsInput.tangent.w, world));
    return vsOutput;
}

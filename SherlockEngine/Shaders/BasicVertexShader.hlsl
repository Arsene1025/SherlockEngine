#include "Common.hlsli"

VSOutput VS_Main(VSInput vsInput)
{
    VSOutput vsOutput = (VSOutput)0;

    // 행벡터 곱 mul(v, M). C++가 행렬을 전치해서 올렸으므로 이 순서가 맞음.
    const float4 localPosition = float4(vsInput.position, 1.0f);
    const float4 worldPosition = mul(localPosition, world);

    vsOutput.position = mul(worldPosition, viewProj);
    vsOutput.worldPosition = worldPosition.xyz;
    vsOutput.col = vsInput.col;
    vsOutput.uv = vsInput.uv * uvScale;
    // 노멀은 (W⁻¹)ᵀ 로 변환함. 그래야 비균등 스케일에서도 표면과 수직을 유지함.
    vsOutput.normal = normalize(mul(float4(vsInput.normal, 0.0f), worldInvTranspose).xyz);
    // 탄젠트는 표면 "위"의 방향이므로 월드 행렬로 변환함 (방향 벡터, w = 0). 손잡이(w)는 월드 행렬이
    // 반사(행렬식 < 0)이면 뒤집음.
    vsOutput.tangent = float4(normalize(mul(float4(vsInput.tangent.xyz, 0.0f), world).xyz), TangentHandedness(vsInput.tangent.w, world));
    return vsOutput;
}

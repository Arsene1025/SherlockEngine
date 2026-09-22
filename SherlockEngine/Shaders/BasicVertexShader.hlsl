#include "Common.hlsli"

VSOutput VS_Main(VSInput vsInput)
{
    VSOutput vsOutput = (VSOutput)0;

    // 행벡터 곱 mul(v, M). C++가 행렬을 전치해서 올렸으므로 이 순서가 맞다.
    const float4 localPosition = float4(vsInput.position, 1.0f);
    const float4 worldPosition = mul(localPosition, world);

    vsOutput.position = mul(worldPosition, viewProj);
    vsOutput.worldPosition = worldPosition.xyz;
    vsOutput.col = vsInput.col;
    // 노멀은 (W⁻¹)ᵀ 로 변환한다. 비균등 스케일에서도 표면에 수직을 유지한다.
    vsOutput.normal = normalize(mul(float4(vsInput.normal, 0.0f), worldInvTranspose).xyz);

    return vsOutput;
}

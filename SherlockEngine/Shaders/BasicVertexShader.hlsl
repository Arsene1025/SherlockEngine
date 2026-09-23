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
    vsOutput.uv = vsInput.uv * uvScale;
    // 노멀은 (W⁻¹)ᵀ 로 변환한다. 비균등 스케일에서도 표면에 수직을 유지한다.
    vsOutput.normal = normalize(mul(float4(vsInput.normal, 0.0f), worldInvTranspose).xyz);
    // 탄젠트는 표면 "위"의 방향이므로 월드 행렬로 변환한다 (방향 벡터, w = 0). 손잡이는 그대로.
    vsOutput.tangent = float4(normalize(mul(float4(vsInput.tangent.xyz, 0.0f), world).xyz), vsInput.tangent.w);
    // 6단계: 같은 월드 위치를 광원의 눈으로 본 클립 좌표. PS 가 그림자 맵 UV 로 바꾼다.
    vsOutput.shadowPosition = mul(worldPosition, lightViewProj);

    return vsOutput;
}

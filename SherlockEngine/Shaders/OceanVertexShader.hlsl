#include "Mesh.hlsli"
#include "Material.hlsli"
#include "Ocean.hlsli"

// Gerstner 바다 정점 셰이더 (docs/plan/ocean-gerstner-plan.html). 메인 패스는 OceanPS, G-buffer 패스는 OceanGBufferPS 와 짝을 이룸.
// 출력은 VSOutput + 변위 전 xz·메시 탄젠트 (OceanVSOutput) — PS 가 노멀을 픽셀에서 다시 구함 (3단계).
// 그림자 패스에는 오지 않음 (바다는 그림자를 받기만 함).
OceanVSOutput VS_Ocean(VSInput vsInput)
{
    OceanVSOutput oceanOutput = (OceanVSOutput)0;
    VSOutput vsOutput = (VSOutput)0;

    // 평면 정점을 먼저 월드로 옮긴 뒤 월드 xz 에서 파도를 더함 (Ocean.hlsli 머리 주석).
    const float4 localPosition = float4(vsInput.position, 1.0f);
    const float3 baseNow = mul(localPosition, world).xyz;
    const float3 basePrev = mul(localPosition, prevWorld).xyz;   // 바다를 옮긴 프레임도 맞게
    const OceanSample now = GerstnerSample(baseNow, OCEAN_PHASE_NOW);
    const float4 worldPosition = float4(now.position, 1.0f);
    const float4 prevPosition = float4(GerstnerPosition(basePrev, OCEAN_PHASE_PREV), 1.0f);

    vsOutput.position = mul(worldPosition, viewProj);
    vsOutput.worldPosition = now.position;
    // 속도: 이번 위치와 지난 프레임 위치(지난 위상 + 지난 World)를 지터 없는 행렬로
    vsOutput.currClip = mul(worldPosition, unjitteredViewProj);
    vsOutput.prevClip = mul(prevPosition, prevUnjitteredViewProj);
    vsOutput.col = vsInput.col;
    vsOutput.uv = vsInput.uv * uvScale;
    vsOutput.normal = now.normal;
    // 탄젠트: 메시 탄젠트(UV u 방향)를 월드로 옮긴 xz 를 변위된 표면 위 방향으로 바꿈 (요로 돌린 바다에서도 노멀 맵 방향이 맞게).
    const float3 meshTangent = mul(float4(vsInput.tangent.xyz, 0.0f), world).xyz;
    vsOutput.tangent = float4(OceanSurfaceTangent(now, meshTangent.xz), TangentHandedness(vsInput.tangent.w, world));

    oceanOutput.base = vsOutput;
    oceanOutput.oceanBase = float4(baseNow.xz, meshTangent.xz);
    return oceanOutput;
}

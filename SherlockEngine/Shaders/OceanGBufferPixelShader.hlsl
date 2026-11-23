// Gerstner 바다 G-buffer PS (docs/plan/ocean-gerstner-plan.html 3단계). GBufferPS 와 같고 노멀·탄젠트만 픽셀에서 다시 구한 값
// (Ocean.hlsli 의 OceanPixelInput). GBufferPixelShader.hlsl 을 통째로 include 함 (그 파일의 PS_GBuffer 는 여기서 진입점이 아님).
#include "GBufferPixelShader.hlsl"
#include "Ocean.hlsli"

GBufferOutput PS_OceanGBuffer(OceanVSOutput input, bool isFrontFace : SV_IsFrontFace)
{
    return PS_GBuffer(OceanPixelInput(input), isFrontFace);
}

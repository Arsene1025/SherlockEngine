// Gerstner 바다 G-buffer PS (docs/plan/ocean-gerstner-plan.html 3단계). GBufferPS 와 같고 노멀·탄젠트만 픽셀에서 다시 구한 값
// (Ocean.hlsli 의 OceanPixelInput). GBufferPixelShader.hlsl 을 통째로 include 함 (그 파일의 PS_GBuffer 는 여기서 진입점이 아님).
#define RAIN_NO_WETNESS   // 물은 젖지 않음 (Rain.hlsli 의 젖음·웅덩이·물결을 재질 PS 에서 뺌)
#include "GBufferPixelShader.hlsl"
#include "Ocean.hlsli"

GBufferOutput PS_OceanGBuffer(OceanVSOutput input, bool isFrontFace : SV_IsFrontFace)
{
    return PS_GBuffer(OceanPixelInput(input), isFrontFace);
}

// Gerstner 바다 포워드 PS (docs/plan/ocean-gerstner-plan.html 3단계). BasicPS 와 같은 셰이딩(ShadeForward)에 노멀·탄젠트만
// 픽셀에서 다시 구한 값(Ocean.hlsli 의 OceanPixelInput)을 넣음. BasicPixelShader.hlsl 을 통째로 include 해서 셰이딩 코드를
// 복사하지 않음 (그 파일의 PS_Main 은 여기서 진입점이 아님).
#define RAIN_NO_WETNESS   // 물은 젖지 않음 (Rain.hlsli 의 젖음·웅덩이·물결을 재질 PS 에서 뺌)
#include "BasicPixelShader.hlsl"
#include "Ocean.hlsli"

ForwardOutput PS_Ocean(OceanVSOutput input, bool isFrontFace : SV_IsFrontFace)
{
    return PS_Main(OceanPixelInput(input), isFrontFace);
}

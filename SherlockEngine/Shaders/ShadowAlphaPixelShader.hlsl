// 알파 테스트 그림자 PS (ShadowAlpha.hlsli). 깊이만 쓰는 패스라 색을 내보내지 않고, 버릴 픽셀만 고름:
//   알파 컷아웃: 알파 = 정점 색 × baseColor × 알베도 텍스처의 a (MaterialSurface.hlsli 의 MaterialBaseColor 와 같은 식) < alphaCutoff
//   디졸브: Dissolve.hlsli 의 DissolveClip (메인·G-buffer PS 와 같은 노이즈·문턱)
#include "ShadowAlpha.hlsli"
#include "Dissolve.hlsli"

void PS_ShadowAlpha(ShadowAlphaVSOutput input)
{
    const float alpha = input.col.a * baseColor.a * albedoTexture.Sample(albedoSampler, input.uv).a;
    if (alphaCutoff > 0.0f)
    {
        clip(alpha - alphaCutoff);
    }
    DissolveClip(DissolveNoise(input.uv));
}

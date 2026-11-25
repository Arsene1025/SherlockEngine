#include "UI.hlsli"

float4 PS_UI(UIPSInput input) : SV_TARGET
{
    // 마스크(5단계): 클립 사각형 밖은 버림. 마스크가 없으면 아주 큰 사각형이라 항상 통과
    if (input.canvasPos.x < input.clip.x || input.canvasPos.y < input.clip.y || input.canvasPos.x > input.clip.z || input.canvasPos.y > input.clip.w)
    {
        discard;
    }
    const float4 tex = uiTexture.Sample(uiSampler, input.uv);
    // 글자(2단계): 아틀라스의 R 이 덮임 정도. 그 밖에는 텍스처 × 정점 색
    float4 color = (input.flags != 0u) ? float4(input.color.rgb, input.color.a * tex.r) : tex * input.color;
    return color;
}

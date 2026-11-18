// SSAO 블러 PS (5단계). 4×4 상자 블러 — SSAO 패스의 베이어 4×4 회전 무늬와 주기가 같아, 블러 한 번에 무늬가 평균돼 사라짐.
// 깊이가 많이 다른 이웃(물체 경계 너머)은 빼서 AO 가 경계를 넘어 번지지 않게 함 (깊이 가중 = 간단한 양방향 필터).
#include "Ssao.hlsli"

float PS_SsaoBlur(float4 position : SV_POSITION) : SV_TARGET
{
    const int2 pixel = int2(position.xy);
    const float depth = LoadSsaoDepth(pixel);
    float result = 1.0f;
    if (depth < 1.0f)
    {
        const float centerZ = ViewDepthFromDevice(depth);
        // 같은 표면으로 볼 깊이 차: 거리의 5% + 바이어스 (멀수록 한 픽셀의 깊이 폭이 커짐).
        const float tolerance = centerZ * 0.05f + ssaoBias;
        const int2 size = int2(screenParams.xy);
        float sum = 0.0f;
        float weight = 0.0f;
        [unroll]
        for (int y = -2; y < 2; ++y)
        {
            [unroll]
            for (int x = -2; x < 2; ++x)
            {
                const int2 p = clamp(pixel + int2(x, y), int2(0, 0), size - 1);
                const float z = ViewDepthFromDevice(LoadSsaoDepth(p));
                const float w = abs(z - centerZ) <= tolerance ? 1.0f : 0.0f;
                sum += ssaoInput.Load(int3(p, 0)) * w;
                weight += w;
            }
        }
        result = weight > 0.0f ? sum / weight : ssaoInput.Load(int3(pixel, 0));
    }
    return result;
}

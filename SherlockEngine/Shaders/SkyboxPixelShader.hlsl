#include "Lighting.hlsli"
#include "Sky.hlsli"
#include "Velocity.hlsli"

// 스카이박스 픽셀 셰이더. 정점 셰이더(SkyboxVertexShader)가 보간해 준 월드 방향으로 환경 큐브맵을 샘플함.
// 큐브맵이 sRGB 라벨이면 샘플할 때 하드웨어가 선형으로 풀고, 톤 패스의 sRGB 뷰가 다시 인코딩하므로 원본 색이
// 그대로 보임.
struct SkyOutput
{
    float4 color : SV_Target0;
    float2 velocity : SV_Target1;   // 속도 RT (Velocity.hlsli)
};

SkyOutput PS_Sky(SkyVSOutput input)
{
    SkyOutput o;
    // HDR 큐브(1 을 넘는 값)도 옴. 선형 값 그대로 FP16 타깃에 씀 — 노출·톤매핑은 TonemapPixelShader 가 함.
    o.color = float4(environmentMap.Sample(environmentSampler, input.direction).rgb, 1.0f);
    // 속도: 하늘은 무한히 먼 방향이라 w = 0 으로 투영함 (이동 행이 곱해지지 않음 → 카메라 회전만 하늘을 움직임).
    // 직교 투영은 모든 픽셀의 방향이 같고 w = 0 이면 나눌 수 없으므로 0.
    const bool orthographic = proj[3][3] > 0.5f;
    o.velocity = orthographic ? float2(0.0f, 0.0f)
        : VelocityUV(mul(float4(input.direction, 0.0f), unjitteredViewProj), mul(float4(input.direction, 0.0f), prevUnjitteredViewProj));
    return o;
}

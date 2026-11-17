#include "Common.hlsli"

// GPU 파티클 픽셀 셰이더 (VFX). 텍스처 없이 쿼드 안에서 원형으로 부드럽게 줄어드는 점 — (1 − r²)².
//
// 블렌드는 프리멀티플라이드 (PSO: 색 One + InvSrcAlpha, 알파는 기존 값 유지) 한 가지로 두 모드를 함:
//   가산 (blendMode 0): 알파 0 을 내보냄 → 목적지 × 1 + 색. 순서와 상관없어 정렬하지 않음.
//   알파 (blendMode 1): 색 × 알파, 알파 → 목적지 × (1 − 알파) + 색. 먼 것부터 그려야 함 (뷰마다 GPU 정렬).
// 소프트 파티클 (softDistance > 0): 씬 깊이(t23)와 파티클의 뷰 z 차이가 그 거리보다 작으면 줄여, 바닥·벽과 만나는
// 선이 딱 끊기지 않게 함.
float4 PS_Particle(ParticleVSOutput input) : SV_TARGET
{
    const float r2 = dot(input.corner, input.corner);
    if (r2 >= 1.0f) discard;
    float falloff = 1.0f - r2;
    falloff *= falloff;

    if (particleSoftDistance > 0.0f)
    {
        const float sceneDepth = particleSceneDepth.Load(int3(input.position.xy, 0));
        const float sceneZ = LinearViewDepth(sceneDepth, proj._m22, proj._m32, proj._m23);
        falloff *= saturate((sceneZ - input.depthFade.x) / particleSoftDistance);
    }

    if (particleBlendMode == 0) return float4(input.color * falloff, 0.0f);
    const float coverage = falloff * particleOpacity;   // color 에는 페이드가 이미 곱해져 있음
    return float4(input.color * coverage, input.depthFade.y * coverage);
}

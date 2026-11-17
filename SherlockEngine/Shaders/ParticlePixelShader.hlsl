#include "Particle.hlsli"

// GPU 파티클 픽셀 셰이더 (VFX). 텍스처 없이 쿼드 안에서 원형으로 부드럽게 줄어드는 점 — (1 − r²)².
//
// 블렌드는 프리멀티플라이드 (PSO: 색 One + InvSrcAlpha, 알파는 기존 값 유지) 한 가지로 두 모드를 함:
//   가산 (blendMode 0): 알파 0 을 내보냄 → 목적지 × 1 + 색. 순서와 상관없어 정렬하지 않음.
//   알파 (blendMode 1): 색 × 알파, 알파 → 목적지 × (1 − 알파) + 색. 먼 것부터 그려야 함 (뷰마다 GPU 정렬).
// 소프트 파티클 (softDistance > 0): 씬 깊이(t23)와 파티클의 뷰 z 차이가 그 거리보다 작으면 줄여, 바닥·벽과 만나는
// 선이 딱 끊기지 않게 함.
float4 PS_Particle(ParticleVSOutput input) : SV_TARGET
{
    //쿼드 중심에서의 거리²
    const float r2 = dot(input.corner, input.corner);
    //원 밖은 버림
    if (r2 >= 1.0f) discard;
    //(1 - r²)²: 가운데 1, 가장자리 0 으로 부드럽게
    float falloff = 1.0f - r2;
    falloff *= falloff;

    //소프트 파티클: 뒤 표면과 가까울수록 흐리게
    if (particleSoftDistance > 0.0f)
    {
        //같은 픽셀의 씬 깊이 (t23)
        const float sceneDepth = particleSceneDepth.Load(int3(input.position.xy, 0));
        //proj 행렬에서 a, b, 원근 여부를 꺼내 뷰 z 로
        const float sceneZ = LinearViewDepth(sceneDepth, proj._m22, proj._m32, proj._m23);
        //(표면 z - 입자 z) / 거리. 표면에 닿으면 0, 거리 이상 떨어지면 1
        falloff *= saturate((sceneZ - input.depthFade.x) / particleSoftDistance);
    }

    //가산: 알파 0 → 목적지 × 1 + 색
    if (particleBlendMode == 0) return float4(input.color * falloff, 0.0f);
    //알파: 프리멀티플라이드라 색에 coverage 를 곱해서 내보냄
    const float coverage = falloff * particleOpacity;   // color 에는 페이드가 이미 곱해져 있음
    return float4(input.color * coverage, input.depthFade.y * coverage);
}

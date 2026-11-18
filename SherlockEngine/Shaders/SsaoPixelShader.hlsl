// SSAO 패스 PS (5단계). 풀스크린 삼각형, 출력 R8_UNORM (1 = 가림 없음). 설명은 Ssao.hlsli 머리 주석.
#include "Ssao.hlsli"

// 4×4 베이어 행렬 (0..15). 픽셀마다 커널을 노멀 축으로 다른 각도만큼 돌려, 적은 샘플로 생기는 띠(banding)를 4×4 격자
// 무늬의 노이즈로 바꿈 — 바로 뒤의 4×4 블러가 그 무늬를 지움. 난수 텍스처 대신 상수라 결정적 (스크린샷 비교).
static const float BAYER4[16] = { 0.0f, 8.0f, 2.0f, 10.0f, 12.0f, 4.0f, 14.0f, 6.0f, 3.0f, 11.0f, 1.0f, 9.0f, 15.0f, 7.0f, 13.0f, 5.0f };

float PS_Ssao(float4 position : SV_POSITION) : SV_TARGET
{
    const int2 pixel = int2(position.xy);
    const float depth = LoadSsaoDepth(pixel);
    float result = 1.0f;
    if (depth < 1.0f)
    {
        // (1) 이 픽셀의 뷰 공간 위치와 노멀 (G-buffer 의 셰이딩 노멀 = 노말맵 적용. 월드 → 뷰 회전).
        const float3 P = ViewPositionFromDevice(position.xy * screenParams.zw, depth);
        const float3 worldNormal = DecodeOct(ssaoNormals.Load(int3(pixel, 0)).xy);
        const float3 N = normalize(mul(worldNormal, (float3x3)view));

        // (2) 노멀을 축으로 픽셀마다 다른 각도의 탄젠트 기저 (그람-슈미트). 임의 벡터가 노멀과 거의 평행하면 다른 축으로.
        const float angle = BAYER4[(pixel.y & 3) * 4 + (pixel.x & 3)] * (2.0f * PI / 16.0f);
        float3 randomVector = float3(cos(angle), sin(angle), 0.0f);
        if (abs(dot(randomVector, N)) > 0.99f) randomVector = float3(0.0f, sin(angle), cos(angle));
        const float3 T = normalize(randomVector - N * dot(randomVector, N));
        const float3 B = cross(N, T);

        // (3) 반구 샘플: 점을 투영해 그 화면 위치의 장면 깊이와 비교. LH 뷰 공간이라 z 가 클수록 멀다 → 장면 z 가 점 z 보다
        //     (바이어스만큼) 작으면 점 앞에 표면이 있음 = 가림. 범위 검사: 반경보다 훨씬 앞의 물체(배경 앞을 지나는 기둥 등)
        //     는 가림으로 세지 않게 smoothstep 으로 줄임.
        const uint count = clamp(ssaoSampleCount, 1u, MAX_SSAO_SAMPLES);
        float occlusion = 0.0f;
        [loop]
        for (uint i = 0; i < count; ++i)
        {
            const float3 k = ssaoKernel[i].xyz;
            const float3 samplePosition = P + (T * k.x + B * k.y + N * k.z) * ssaoRadius;
            const float4 clip = mul(float4(samplePosition, 1.0f), proj);
            const float2 uv = float2(clip.x / clip.w * 0.5f + 0.5f, -clip.y / clip.w * 0.5f + 0.5f);
            if (clip.w <= 0.0f || any(uv < 0.0f) || any(uv > 1.0f)) continue;
            const float sceneZ = ViewDepthFromDevice(LoadSsaoDepth(int2(uv * screenParams.xy)));
            const float rangeCheck = smoothstep(0.0f, 1.0f, ssaoRadius / max(abs(P.z - sceneZ), EPSILON));
            occlusion += (sceneZ <= samplePosition.z - ssaoBias ? 1.0f : 0.0f) * rangeCheck;
        }
        // (4) 가려진 비율 → AO. 세기와 거듭제곱으로 대비를 조절함.
        result = pow(saturate(1.0f - occlusion / count * ssaoIntensity), ssaoPower);
    }
    return result;
}

// 디퍼드 라이팅 패스 PS (2단계). 화면을 덮는 삼각형(ScreenVS) 하나로, 픽셀마다 G-buffer 를 읽어 포워드와 같은
// 조명 함수(Shading.hlsli)를 한 번 부름. 출력은 view.hdr 에 가산 블렌딩 (One + One) — G-buffer 패스가 써 둔 발광 위에
// 환경광 + 직접광이 더해짐. 깊이 1 (아무것도 안 그려진 배경)은 버림 — 하늘은 뒤의 포워드 패스가 그림.
//
// 입력: G-buffer 셋 (GBuffer.hlsli: t23 깊이, t25~t28, t29 SSAO) + 프레임 셋 (조명·그림자·IBL·클러스터).
#include "GBuffer.hlsli"

float4 PS_DeferredLighting(float4 position : SV_POSITION) : SV_TARGET
{
    const int2 pixel = int2(position.xy);
    const float depth = gbufferDepth.Load(int3(pixel, 0));
    if (depth >= 1.0f)
    {
        discard;
    }

    // 위치는 저장하지 않고 깊이에서 복원함 (GBuffer.hlsli).
    const float3 worldPosition = ReconstructWorldPosition(position.xy, depth);
    const SurfaceData s = LoadGBufferSurface(pixel);
    const float3 viewDirection = SafeNormalize(cameraPosition - worldPosition, float3(0.0f, 0.0f, -1.0f));
    const float viewZ = ViewDepthFromDevice(depth);

    // SSAO (5단계). 꺼져 있으면 1. 재질 AO 와 합치는 방식: 기본 min (같은 가림을 두 번 세지 않음), 또는 곱.
    const float ssao = aoParams.x > 0.5f ? ssaoTexture.Load(int3(pixel, 0)) : 1.0f;
    const float ao = aoParams.y > 0.5f ? s.ao * ssao : min(s.ao, ssao);

    // 디버그 뷰. 알파 0 으로 내보냄 — 블렌드가 알파는 기존 값을 유지함 (Zero, One).
    const int debugView = (int)debugParams.x;
    float3 debugColor;
    if (SurfaceDebugColor(debugView, s, worldPosition, debugColor)) return float4(debugColor, 0.0f);
    if (ClusterDebugColor(debugView, position.xy, viewZ, debugColor)) return float4(debugColor, 0.0f);
    if (debugView == 13) return float4(ssao, ssao, ssao, 0.0f);

    // 환경광은 AO 를 받고 (가려진 곳에 하늘빛이 덜 듦), 직접광은 받지 않음 (그림자가 그 역할을 함).
    const float3 environment = ShadeEnvironment(s, viewDirection, ao);
    const float3 color = ShadeLights(s, worldPosition, viewDirection, position.xy, viewZ, environment);
    return float4(color, 0.0f);
}

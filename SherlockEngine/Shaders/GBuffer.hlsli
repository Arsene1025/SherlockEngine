// 디퍼드 G-buffer (2단계): 배치·인코딩과 G-buffer 셋 (Layout_GBuffer, 뷰마다).
//
// G-buffer 패스(GBufferPixelShader.hlsl)가 MRT 6장에 쓰고, 디퍼드 라이팅 패스(DeferredLightingPixelShader.hlsl)가
// 픽셀마다 Load 로 읽어 SurfaceData 를 되살림. 위치는 저장하지 않고 깊이에서 복원함.
//
//   SV_Target0  view.hdr  R16G16B16A16_FLOAT   rgb = 발광 (라이팅 패스가 그 위에 가산), a = 기본색 알파
//   SV_Target1  GB0       R8G8B8A8_UNORM_SRGB  rgb = albedo,  a = 재질 AO
//   SV_Target2  GB1       R16G16B16A16_FLOAT   xy  = 셰이딩 노멀 (옥타헤드럴), zw = 기하 노멀 (옥타헤드럴)
//   SV_Target3  GB2       R8G8B8A8_UNORM_SRGB  rgb = F0,      a = 거칠기
//   SV_Target4  GB3       R8G8B8A8_UNORM       r = 금속성, g = F90, b = 클리어코트, a = 코팅 거칠기
//   SV_Target5  view.velocity R16G16_FLOAT     xy = 속도 (Velocity.hlsli). 라이팅 패스는 읽지 않음 (모션 블러·TAA 가 읽음)
//   (깊이)      view.depth D24S8                위치 복원의 입력
//
// sRGB 포맷은 쓸 때 선형 → sRGB, 읽을 때 sRGB → 선형을 하드웨어가 함 (어두운 값에 정밀도를 더 줌 — albedo 와 F0 의
// 작은 값). 알파 채널은 sRGB 변환을 하지 않음.
//
// G-buffer 셋 (라이팅 PS 만): t23 깊이, t25~t28 위의 GB0~GB3, t29 SSAO 블러 결과 (SSAO 타깃을 못 만든 뷰는
// 흰색 1×1. SSAO 가 꺼진 뷰는 b0 의 aoParams.x 가 0 이라 읽지 않음).
#ifndef SHERLOCK_GBUFFER_HLSLI
#define SHERLOCK_GBUFFER_HLSLI

#include "Shading.hlsli"

Texture2D<float> gbufferDepth : register(t23);
Texture2D<float4> gbuffer0 : register(t25);
Texture2D<float4> gbuffer1 : register(t26);
Texture2D<float4> gbuffer2 : register(t27);
Texture2D<float4> gbuffer3 : register(t28);
Texture2D<float> ssaoTexture : register(t29);

// 옥타헤드럴 인코딩(EncodeOct/DecodeOct)과 위치 복원(ReconstructWorldPosition)은 Common.hlsli 에 있음 — SSAO 도 씀.

// G-buffer 의 한 픽셀 → SurfaceData. 발광은 이미 view.hdr 에 있으므로 0, 투과·코팅 노멀은 디퍼드로 오지 않음
// (CanUseDeferred 가 그런 재질을 포워드로 보냄) — 투과 0, 코팅 노멀 = 기하 노멀.
SurfaceData LoadGBufferSurface(int2 pixel)
{
    const float4 g0 = gbuffer0.Load(int3(pixel, 0));
    const float4 g1 = gbuffer1.Load(int3(pixel, 0));
    const float4 g2 = gbuffer2.Load(int3(pixel, 0));
    const float4 g3 = gbuffer3.Load(int3(pixel, 0));

    SurfaceData s;
    s.albedo = g0.rgb;
    s.alpha = 1.0f;
    s.ao = g0.a;
    s.normal = DecodeOct(g1.xy);
    s.geometricNormal = DecodeOct(g1.zw);
    s.F0 = g2.rgb;
    s.roughness = max(g2.a, 0.03f);   // 8비트 양자화로 하한 아래로 내려가지 않게 (재질 쪽 clamp 와 같은 하한)
    s.metallic = g3.r;
    s.F90 = g3.g;
    s.clearcoat = g3.b;
    s.clearcoatRoughness = max(g3.a, 0.03f);
    s.clearcoatNormal = s.geometricNormal;
    s.emission = float3(0.0f, 0.0f, 0.0f);
    s.transmission = 0.0f;
    return s;
}

#endif // SHERLOCK_GBUFFER_HLSLI

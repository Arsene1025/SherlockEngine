// G-buffer 패스 PS (2단계 디퍼드). 재질을 포워드와 같은 함수로 읽어(MaterialSurface.hlsli) MRT 5장에 나눠 씀.
// 조명은 계산하지 않음 — 디퍼드 라이팅 패스가 화면 픽셀마다 한 번만 함 (오버드로가 조명 비용을 늘리지 않음).
// 배치와 인코딩은 GBuffer.hlsli 머리 주석. 이 패스로 오는 재질: 불투명·컷아웃, unlit·투과·코팅 노멀 맵이 없는 것
// (C++ Renderer::CanUseDeferred).
#include "MaterialSurface.hlsli"
#include "GBuffer.hlsli"
#include "Velocity.hlsli"

struct GBufferOutput
{
    float4 emission : SV_Target0;   // view.hdr: 라이팅 패스가 이 위에 환경광 + 직접광을 가산함
    float4 gb0 : SV_Target1;
    float4 gb1 : SV_Target2;
    float4 gb2 : SV_Target3;
    float4 gb3 : SV_Target4;
    float2 velocity : SV_Target5;   // view.velocity R16G16_FLOAT (Velocity.hlsli)
};

GBufferOutput PS_GBuffer(VSOutput input, bool isFrontFace : SV_IsFrontFace)
{
    const float4 surface = MaterialBaseColor(input);
    // 알파 컷아웃 (glTF MASK). 구멍 난 픽셀은 깊이도 G-buffer 도 쓰지 않음.
    if (alphaCutoff > 0.0f)
    {
        clip(surface.a - alphaCutoff);
    }
    const SurfaceData s = EvaluateMaterialSurface(input, isFrontFace, surface);

    GBufferOutput o;
    // 디버그 뷰가 켜져 있으면 발광을 쓰지 않음 — 라이팅 패스가 디버그 색을 가산하므로 그 아래가 0 이어야 포워드와 같은 색.
    const bool debugView = debugParams.x > 0.5f;
    o.emission = float4(debugView ? float3(0.0f, 0.0f, 0.0f) : s.emission, surface.a);
    o.gb0 = float4(s.albedo, s.ao);
    o.gb1 = float4(EncodeOct(s.normal), EncodeOct(s.geometricNormal));
    o.gb2 = float4(s.F0, s.roughness);
    o.gb3 = float4(s.metallic, s.F90, s.clearcoat, s.clearcoatRoughness);
    o.velocity = VelocityUV(input.currClip, input.prevClip);
    return o;
}

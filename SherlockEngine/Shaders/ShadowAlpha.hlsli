// 알파 테스트 그림자 (그림자 패스의 PS 변형, docs/plan/object-highlight-record.html 7장). 셋은 메인 패스처럼 프레임·오브젝트·재질 셋.
// 알파 컷아웃 재질(Material::alphaCutoff > 0)과 디졸브 중인 오브젝트의 그림자만 이 경로로 그림 — 나머지는 PS 없는 Shadow/ShadowSkin 패스.
//   ShadowAlphaVertexShader.hlsl      VS_ShadowAlpha       위치 + 정점 색 + UV (uvScale 곱함, BasicVS 와 같음)
//   ShadowAlphaSkinVertexShader.hlsl  VS_ShadowAlphaSkin   위 + 스키닝
//   ShadowAlphaPixelShader.hlsl       PS_ShadowAlpha       컷아웃·디졸브 clip 만 (색 출력 없음)
#ifndef SHERLOCK_SHADOW_ALPHA_HLSLI
#define SHERLOCK_SHADOW_ALPHA_HLSLI

#include "Mesh.hlsli"
#include "Lighting.hlsli"   // b5 shadowViewProj
#include "Material.hlsli"   // b3 uvScale·baseColor·alphaCutoff, t0 알베도, t20 노이즈, s0

struct ShadowAlphaVSOutput
{
    float4 position : SV_POSITION;
    float4 col : COLOR0;      // 정점 색 (알파가 컷아웃에 들어감 — MaterialBaseColor 와 같은 식)
    float2 uv : TEXCOORD0;
};

#endif // SHERLOCK_SHADOW_ALPHA_HLSLI

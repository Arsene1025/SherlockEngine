// 스카이박스 VS → PS (SkyboxVertexShader / SkyboxPixelShader). 환경 큐브(t9)·샘플러(s5)는 Lighting.hlsli.
#ifndef SHERLOCK_SKY_HLSLI
#define SHERLOCK_SKY_HLSLI

struct SkyVSOutput
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD0;   // 월드 공간 방향 (정규화 전). 큐브 샘플은 방향만 보므로 길이는 상관없음
};

#endif // SHERLOCK_SKY_HLSLI

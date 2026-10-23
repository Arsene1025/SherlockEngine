#include "Common.hlsli"

// 2026-10-20: 톤매핑 패스 픽셀 셰이더 (ScreenVertexShader 와 짝). HDR 씬 텍스처를 같은 픽셀 좌표로 읽어 노출·ACES 를 적용함.
// 샘플러 없이 Load 를 쓰는 이유: 입력과 출력이 같은 크기라 1:1 대응이고, 재질 샘플러(s0) 등과 슬롯이 겹칠 일도 없음.
// 결과는 선형 — 이 패스의 sRGB RTV 가 인코딩함.
Texture2D sceneHdr : register(t13);

float4 PS_Tonemap(float4 position : SV_POSITION) : SV_TARGET
{
    const float3 hdr = sceneHdr.Load(int3((int2)position.xy, 0)).rgb;

    // 디버그 뷰(알베도·노멀 등)는 조명 결과가 아니므로 노출·톤매핑을 거치지 않음 (기존에도 톤매핑 전에 반환했음).
    if (debugParams.x > 0.5f)
    {
        return float4(saturate(hdr), 1.0f);
    }
    return float4(ApplyExposureToneMap(hdr), 1.0f);
}

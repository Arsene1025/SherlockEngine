#include "Common.hlsli"

// 6단계: 그림자 맵 패스. 깊이만 쓰므로 픽셀 셰이더가 없고 위치만 출력함.
// 입력 레이아웃은 메인 패스와 같은 VERTEX(64B)를 씀. VS 가 POSITION 만 읽어도
// VS 시그니처의 모든 원소가 레이아웃에 있기만 하면 D3D11 입력 레이아웃 검증을 통과함.
struct ShadowVSInput
{
    float3 position : POSITION;
};

float4 VS_Shadow(ShadowVSInput input) : SV_POSITION
{
    const float4 worldPosition = mul(float4(input.position, 1.0f), world);
    return mul(worldPosition, lightViewProj);
}

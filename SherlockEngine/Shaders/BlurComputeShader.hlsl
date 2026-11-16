#include "Common.hlsli"

// 가우시안 블러 한 방향의 컴퓨트 버전 (PostPixelShader.hlsl 모드 1 과 짝). 그래픽스 경로의 풀스크린 삼각형 대신
// 스레드 하나가 출력 픽셀 하나를 맡음. 수식은 PS 와 글자 그대로 같음 — 같은 Load, 같은 가중치 계산, 같은 더하기
// 순서라서 두 경로의 결과가 (거의) 비트 단위로 같아야 함. 그 비교가 이 셰이더의 첫 번째 검증임.
//
//   t13 : 입력 (가로 패스 = hdr, 세로 패스 = 스크래치). SRV 로만 읽음 — cs_5_0 은 float4 UAV 를 읽을 수 없음.
//   u0  : 출력 (가로 패스 = 스크래치, 세로 패스 = hdr). 같은 Dispatch 에서 입력과 같은 텍스처가 되지 않음.
//   b4  : PostParams (postPass.yz = 방향, postBlur.xy = 시그마·반경, postView.xy = 입력 크기, zw = 출력 크기)
//
// 그룹은 8×8 = 64 스레드. Dispatch 는 (ceil(w/8), ceil(h/8), 1) 이라 가장자리 그룹에는 텍스처 밖 스레드가 생김 —
// 그 스레드는 아무것도 쓰지 않고 끝냄.
Texture2D<float4> blurInput : register(t13);
RWTexture2D<float4> blurOutput : register(u0);

static const int kMaxBlurRadius = 64;   // PostPixelShader.hlsl·RendererPost.cpp 와 같아야 함

[numthreads(8, 8, 1)]
void CS_Blur(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)postView.z || id.y >= (uint)postView.w) return;

    const int2 base = (int2)id.xy;
    const int2 maxCoord = (int2)postView.xy - 1;
    const int2 dir = int2((int)postPass.y, (int)postPass.z);
    const int radius = min((int)postBlur.y, kMaxBlurRadius);
    const float sigma = max(postBlur.x, 0.01f);
    const float invTwoSigmaSq = 0.5f / (sigma * sigma);

    float3 sum = 0.0f;
    float weightSum = 0.0f;
    for (int i = -radius; i <= radius; ++i)
    {
        const float w = exp(-(float)(i * i) * invTwoSigmaSq);
        const int2 c = clamp(base + dir * i, int2(0, 0), maxCoord);
        sum += blurInput.Load(int3(c, 0)).rgb * w;
        weightSum += w;
    }
    blurOutput[id.xy] = float4(sum / weightSum, 1.0f);
}

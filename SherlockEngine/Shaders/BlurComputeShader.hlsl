#include "Post.hlsli"

// 가우시안 블러 한 방향의 컴퓨트 버전 (PostPixelShader.hlsl 모드 1 과 짝). 그래픽스 경로의 풀스크린 삼각형 대신
// 스레드 하나가 출력 픽셀 하나를 맡음. 수식은 PS 와 글자 그대로 같음 — 같은 Load, 같은 가중치 버퍼(t20), 같은 더하기
// 순서라서 두 경로의 결과가 (거의) 같아야 함. 그 비교가 이 셰이더의 첫 번째 검증임. 가중치를 루프 안에서 따로 계산하던
// 때는 D3D11 에서 CS 와 PS 가 2366 픽셀 최하위 비트만큼 달랐고, 가중치 버퍼를 같이 쓰면서 1 픽셀로 줄었음 (D3D12 는 0).
// 남은 차이는 누적을 precise 로 막으면 0 이 되지만 (곱셈+덧셈을 mad 로 합치는지가 스테이지마다 다른 것으로 보임),
// 그러면 PS 출력도 바뀌어서 넣지 않음. Debug 셰이더, 이 GPU 에서 확인.
//
//   t13 : 입력 (가로 패스 = hdr, 세로 패스 = 스크래치). SRV 로만 읽음 — cs_5_0 은 float4 UAV 를 읽을 수 없음.
//   u0  : 출력 (가로 패스 = 스크래치, 세로 패스 = hdr). 같은 Dispatch 에서 입력과 같은 텍스처가 되지 않음.
//   b4  : PostParams (postPass.yz = 방향, postBlur.y = 반경, postView.xy = 입력 크기, zw = 출력 크기)
//   t20 : 가중치 (Post.hlsli 의 blurWeights)
//
// 그룹은 8×8 = 64 스레드. Dispatch 는 (ceil(w/8), ceil(h/8), 1) 이라 가장자리 그룹에는 텍스처 밖 스레드가 생김 —
// 그 스레드는 아무것도 쓰지 않고 끝냄.
RWTexture2D<float4> blurOutput : register(u0);

[numthreads(8, 8, 1)]
void CS_Blur(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)postView.z || id.y >= (uint)postView.w) return;

    const int2 base = (int2)id.xy;
    const int2 maxCoord = (int2)postView.xy - 1;
    const int2 dir = int2((int)postPass.y, (int)postPass.z);
    const int radius = min((int)postBlur.y, kMaxBlurRadius);

    float3 sum = 0.0f;
    float weightSum = 0.0f;
    for (int i = -radius; i <= radius; ++i)
    {
        const float w = blurWeights[abs(i)];
        const int2 c = clamp(base + dir * i, int2(0, 0), maxCoord);
        sum += postInput.Load(int3(c, 0)).rgb * w;
        weightSum += w;
    }
    blurOutput[id.xy] = float4(sum / weightSum, 1.0f);
}

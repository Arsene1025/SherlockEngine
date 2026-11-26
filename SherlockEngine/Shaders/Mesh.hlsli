// 메시 그리기: 오브젝트 셋 (Layout_Object) 과 정점 입출력.
// 상수버퍼 배치는 Graphics/ShaderConstants.h 와 바이트 단위로 같아야 함.
//
//   b1 PerObject (VS·PS), b7 Skin (스킨 VS), b13 PrevSkin (스킨 VS, 속도 버퍼)
#ifndef SHERLOCK_MESH_HLSLI
#define SHERLOCK_MESH_HLSLI

#include "Common.hlsli"

// b1: 드로우마다. 행렬은 VS, 강조 색·디졸브는 PS (MaterialSurface.hlsli, Dissolve.hlsli). (256바이트 — 넘기지 말 것, ShaderConstants.h 참고)
cbuffer PerObject : register(b1)
{
    matrix world;
    matrix worldInvTranspose;
    matrix prevWorld;           // 지난 프레임 World. 처음 보는 오브젝트·그림자 패스는 world 와 같음 (속도 0)
    // 오브젝트별 강조 (C++ Scene/Highlight.h). 강조가 없으면 모두 0.
    float4 highlightTint;       // rgb = 섞을 색 (선형), a = 섞는 양 [0, 1]
    float4 highlightFlash;      // rgb = 발광에 더할 색 × 세기, a = 디졸브 경계 폭 (노이즈 값 단위. 남는 칸을 빌림)
    float4 highlightRim;        // rgb = 가장자리 빛 색 × 세기, a = 지수 (C++ 가 0.1 이상으로 자름)
    // 디졸브 (C++ Scene/Dissolve.h, Dissolve.hlsli). 디졸브가 없으면 모두 0.
    float4 dissolveEdge;        // rgb = 경계 빛 색 × 세기, a = 진행도 [0, 1] (0 = 끔)
}

// b7: 스키닝 조인트 행렬 팔레트. 스킨 VS(BasicSkinVertexShader, ShadowSkinVertexShader)만 읽음. (16384바이트)
// palette[j] = 역바인드 · 조인트 월드 · (메시 노드 월드)⁻¹ 를 C++ 가 전치해서 올림 (world 와 같은 관례, mul(v, M)).
static const uint MAX_JOINTS = 256;   // ShaderConstants.h 의 kMaxSkinJoints 와 같아야 함
cbuffer Skin : register(b7)
{
    matrix joints[MAX_JOINTS];
}

// 정점의 조인트 번호 4개·가중치 4개로 섞은 스킨 행렬. 가중치 합이 1 이 되게 임포터가 맞춰 둠.
matrix SkinMatrix(uint4 jointIndices, float4 jointWeights)
{
    return joints[jointIndices.x] * jointWeights.x
         + joints[jointIndices.y] * jointWeights.y
         + joints[jointIndices.z] * jointWeights.z
         + joints[jointIndices.w] * jointWeights.w;
}

// b13: 지난 프레임 팔레트 (b7 과 같은 배치). 메인·G-buffer 스킨 VS 가 속도용 이전 위치를 만들 때만 읽음. 지난 프레임이
// 없는 오브젝트는 C++ 가 이번 팔레트를 그대로 올림 (속도 0).
cbuffer PrevSkin : register(b13)
{
    matrix prevJoints[MAX_JOINTS];
}

matrix PrevSkinMatrix(uint4 jointIndices, float4 jointWeights)
{
    return prevJoints[jointIndices.x] * jointWeights.x
         + prevJoints[jointIndices.y] * jointWeights.y
         + prevJoints[jointIndices.z] * jointWeights.z
         + prevJoints[jointIndices.w] * jointWeights.w;
}

struct VSInput
{
    float3 position : POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;   // xyz = u 증가 방향, w = 손잡이. B = cross(N, T) * w
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float4 tangent : TANGENT0;
    // 속도 버퍼: 같은 정점을 이번·지난 프레임의 지터 없는 행렬로 투영한 클립 좌표 (Velocity.hlsli 의 VelocityUV 가 받음)
    float4 currClip : TEXCOORD2;
    float4 prevClip : TEXCOORD3;
};

// 월드 행렬이 반사(행렬식 < 0, 예: scale -1 미러)이면 노멀((W⁻¹)ᵀ)과 탄젠트(W)가 따로 변환되어 cross(N, T) 의
// 방향이 뒤집힘. 그래서 탄젠트 손잡이 w 에 행렬식의 부호를 곱해야 B = cross(N, T) * w 가 표면을 제대로
// 따라감. 스케일 0 이어도 0 이 되지 않도록 sign() 대신 삼항을 씀. 행렬식 부호는 전치해도 같아서 C++ 가 전치해
// 올린 world 를 그대로 써도 됨. 탄젠트를 출력하는 모든 VS(디퍼드 G-buffer 포함)가 이 함수를 거칠 것.
float TangentHandedness(float tangentW, matrix worldMatrix)
{
    const float det = dot(cross(worldMatrix[0].xyz, worldMatrix[1].xyz), worldMatrix[2].xyz);
    return det < 0.0f ? -tangentW : tangentW;
}

#endif // SHERLOCK_MESH_HLSLI

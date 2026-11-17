// 메시 그리기: 오브젝트 셋 (Layout_Object) 과 정점 입출력.
// 상수버퍼 배치는 Graphics/ShaderConstants.h 와 바이트 단위로 같아야 함.
//
//   b1 PerObject (VS), b7 Skin (스킨 VS)
#ifndef SHERLOCK_MESH_HLSLI
#define SHERLOCK_MESH_HLSLI

#include "Common.hlsli"

// b1: 드로우마다. VS만. (128바이트)
cbuffer PerObject : register(b1)
{
    matrix world;
    matrix worldInvTranspose;
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

// 파티클 시뮬레이션 셋 (Layout_ParticleSim, 이미터마다) 과 충돌 셋 (Layout_ParticleCollide). 컴퓨트 셰이더 4개
// (Reset·Sim·Args·Emit) 가 같이 씀.
#ifndef SHERLOCK_PARTICLE_SIM_HLSLI
#define SHERLOCK_PARTICLE_SIM_HLSLI

#include "Particle.hlsli"

// u0·u1 이 정렬 셋 (ParticleSort.hlsli) 과 다른 리소스라 한 셰이더가 둘을 같이 쓰면 안 됨.
#ifdef SHERLOCK_PARTICLE_SORT_HLSLI
#error ParticleSim.hlsli 와 ParticleSort.hlsli 는 u0, u1 의 의미가 달라 한 셰이더에 같이 include 할 수 없음
#endif

//   u0 particlesRW      파티클 capacity 개
//   u1 particleDead     사망 목록: 비어 있는 슬롯 번호. [0, 카운터 Dead) 가 유효
//   u2 particleAliveIn  지난 프레임 생존 목록 (시뮬레이션이 읽음)
//   u3 particleAliveOut 이번 프레임 생존 목록 (시뮬레이션·생성이 씀, 가산 이미터는 VS 가 t22 로 읽음 — 알파 이미터는 정렬 목록). 프레임마다 u2 와 바뀜
//   u4 particleCounters 개수들 (Particle.hlsli 의 kCounter*)
//   u5 particleArgs     간접 인자 (raw). 바이트 0: 그리기 {6, 생존 수, 0, 0}, 바이트 16: 시뮬레이션 디스패치 {그룹 수, 1, 1}
RWStructuredBuffer<Particle> particlesRW : register(u0);
RWStructuredBuffer<uint> particleDead : register(u1);
RWStructuredBuffer<uint> particleAliveIn : register(u2);
RWStructuredBuffer<uint> particleAliveOut : register(u3);
RWStructuredBuffer<uint> particleCounters : register(u4);
RWByteAddressBuffer particleArgs : register(u5);

//args 버퍼 안의 바이트 위치: 0 그리기 인자, 16 Sim 디스패치 인자
//C++ 의 kParticleArgsDrawOffset·kParticleArgsDispatchOffset 과 같아야 함
static const uint kArgsDrawOffset = 0;
static const uint kArgsDispatchOffset = 16;
//Sim 의 numthreads 와 같아야 함 (Args 가 다음 Sim 그룹 수를 이 값으로 계산)
static const uint kParticleSimGroupSize = 64;

// b9: 파티클 충돌 (ShaderConstants.h 의 ParticleCollisionConstants). 메인 뷰 카메라, 프레임에 한 번.
cbuffer ParticleCollision : register(b9)
{
    //메인 카메라 viewProj (입자 위치 → 화면)
    matrix collisionViewProj;
    //그 역행렬 (깊이 버퍼 한 점 → 월드 위치, 법선 계산용)
    matrix collisionInvViewProj;
    //LinearViewDepth 에 넘길 투영 값 a, b, 원근 여부
    float collisionDepthA;
    float collisionDepthB;
    float collisionPerspective;
    //표면 뒤 이 거리(m) 안이면 충돌 (C++ 의 kCollisionThickness)
    float collisionThickness;
    //깊이 버퍼 크기 (픽셀)
    uint2 collisionDepthSize;
    uint2 collisionPadding;
}

#endif // SHERLOCK_PARTICLE_SIM_HLSLI

#define PARTICLE_SIM
#include "Common.hlsli"

// GPU 파티클 (VFX 2단계) — 버퍼를 새로 만든 프레임에 한 번. 파티클을 전부 죽은 상태로, 사망 목록을 모든 슬롯으로,
// 개수와 간접 인자를 "아무도 없음"으로 씀. D3D11 은 초기 데이터 없이 만든 버퍼의 내용이 정해져 있지 않음.
//
// Dispatch = ceil(capacity / 64) 그룹.
[numthreads(64, 1, 1)]
void CS_ParticleReset(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i < particleCapacity)
    {
        Particle p;
        p.position = 0.0f;
        p.velocity = 0.0f;
        p.age = 1.0f;
        p.lifetime = 0.0f;
        particlesRW[i] = p;
        // 생성은 목록 끝에서 꺼내므로 (CS_ParticleArgs) 거꾸로 넣어 두면 처음에는 슬롯 0, 1, 2 … 순서로 쓰임.
        particleDead[i] = particleCapacity - 1 - i;
    }
    if (i == 0)
    {
        for (uint c = 0; c < kParticleCounterCount; ++c) particleCounters[c] = 0;
        particleCounters[kCounterDead] = particleCapacity;
        particleArgs.Store4(kArgsDrawOffset, uint4(6, 0, 0, 0));
        particleArgs.Store3(kArgsDispatchOffset, uint3(0, 1, 1));   // 그룹 0 개 = 아무것도 안 함 (허용됨)
    }
}

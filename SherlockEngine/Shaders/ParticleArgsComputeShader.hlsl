#define PARTICLE_SIM
#include "Common.hlsli"

// GPU 파티클 (VFX 2단계) — 시뮬레이션과 생성 사이에 스레드 하나. 개수를 확정하고 간접 인자를 씀:
//   - 이번 프레임에 태어날 수 = min(원하는 수, 사망 목록 길이). 사망 목록이 비면 생성이 멈춤 (1단계는 가장 오래된
//     파티클을 덮어썼음).
//   - 생성은 사망 목록의 끝 구간을 꺼내고 생존 목록 뒤에 이어 붙임. 그 구간 시작을 카운터에 남김 — 생성 스레드 t 가
//     원자 연산 없이 자기 자리를 알아서, 같은 입력이면 같은 슬롯·같은 목록 위치가 나옴.
//   - 그리기 인자 {6, 최종 생존 수, 0, 0} 과 다음 프레임 시뮬레이션의 디스패치 인자.
// 다음 프레임 시뮬레이션의 원자적 카운터(생존)는 여기서 0 으로 되돌림. 생성은 이 카운터를 읽지 않음.
[numthreads(1, 1, 1)]
void CS_ParticleArgs()
{
    const uint dead = particleCounters[kCounterDead];
    const uint alive = particleCounters[kCounterAlive];
    const uint emit = min(particleSpawnCount, dead);
    const uint total = alive + emit;

    particleCounters[kCounterEmitCount] = emit;
    particleCounters[kCounterEmitDeadBase] = dead - emit;
    particleCounters[kCounterEmitAliveBase] = alive;
    particleCounters[kCounterDead] = dead - emit;
    particleCounters[kCounterAlive] = 0;
    particleCounters[kCounterSimCount] = total;

    particleArgs.Store4(kArgsDrawOffset, uint4(6, total, 0, 0));   // DrawInstancedIndirect: 정점 6 × 인스턴스 total
    particleArgs.Store3(kArgsDispatchOffset, uint3((total + kParticleSimGroupSize - 1) / kParticleSimGroupSize, 1, 1));
}

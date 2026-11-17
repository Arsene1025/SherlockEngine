#include "ParticleSim.hlsli"

// GPU 파티클 생성 (VFX 2단계). 스레드 t 가 사망 목록에서 슬롯 하나를 꺼내 새 파티클을 쓰고 생존 목록 뒤에 붙임.
// Dispatch = ceil(원하는 수 / 64) 그룹 (CPU 가 앎). 실제로 태어나는 수는 CS_ParticleArgs 가 정함.
//
// 난수와 프레임 안 출생 시각은 슬롯이 아니라 t (이번 프레임에 몇 번째로 태어났나) 에 묶음 — 1단계의 offset 과 같은
// 식이라, 버퍼가 다 차지 않는 한 1단계와 같은 파티클이 나옴.

// 방향 d 둘레의 원뿔 안에서 고른 단위 벡터. cosθ 를 [spreadCos, 1] 에서 고르게 고르면 구면 위 면적이 고름.
float3 RandomInCone(float3 d, float spreadCos, inout uint rng)
{
    //cosθ 를 1(축 방향) ~ spreadCos 사이에서 고르게
    const float cosTheta = lerp(1.0f, spreadCos, Random01(rng));
    const float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
    //축 둘레 각도 0 ~ 2π
    const float phi = 6.28318530718f * Random01(rng);
    //d 와 평행하지 않은 보조 벡터로 d 에 수직인 축 t, b 를 만듦
    const float3 helper = abs(d.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 t = normalize(cross(helper, d));
    const float3 b = cross(d, t);
    //(θ, φ) → t, b, d 기저의 벡터
    return t * (sinTheta * cos(phi)) + b * (sinTheta * sin(phi)) + d * cosTheta;
}

[numthreads(64, 1, 1)]
void CS_ParticleEmit(uint3 id : SV_DispatchThreadID)
{
    const uint t = id.x;
    //t = 이번 프레임에 몇 번째로 태어나는 입자인지
    //실제 생성 수(Args 가 정함)를 넘는 스레드는 일 없음
    if (t >= particleCounters[kCounterEmitCount]) return;

    //사망 목록 끝 구간(EmitDeadBase 부터)에서 t 번째 칸 번호를 꺼냄
    const uint index = particleDead[particleCounters[kCounterEmitDeadBase] + t];
    //난수 상태: 프레임 씨앗과 t 를 섞음 → 같은 입력이면 같은 입자 (결정적)
    uint rng = PcgHash(t ^ PcgHash(particleSeed));
    //방출 방향 (원뿔 안)
    const float3 direction = RandomInCone(particleDirection, particleSpreadCos, rng);
    //속력: startSpeed 에서 variation 비율만큼 무작위로 줄임
    const float speed = particleStartSpeed * (1.0f - particleSpeedVariation * Random01(rng));
    // 구 안의 고른 점: 방향은 구면에서, 반지름은 세제곱근 (부피가 r³ 에 비례).
    const float3 offsetDir = RandomInCone(float3(0.0f, 1.0f, 0.0f), -1.0f, rng);
    const float3 origin = particleEmitterPosition + offsetDir * (particleEmitRadius * pow(Random01(rng), 1.0f / 3.0f));
    // 프레임 안에서 태어난 시각을 나눔: 앞 번호일수록 일찍 태어나 이미 조금 움직였음. 이렇게 하지 않으면 한 프레임에
    // 태어난 파티클이 한 점에 뭉쳐 프레임마다 "층"이 보임.
    const float age = particleDeltaTime * (1.0f - (t + 0.5f) / particleSpawnCount);
    const float3 velocity = direction * speed;

    //태어난 뒤 age 만큼 이미 움직인 상태로 씀 (등가속도 운동 식)
    Particle p;
    p.position = origin + velocity * age + 0.5f * particleGravity * age * age;
    p.velocity = velocity + particleGravity * age;
    p.age = age;
    p.lifetime = particleLifetime;
    particlesRW[index] = p;
    //생존 목록에서 Sim 이 채운 칸 뒤(EmitAliveBase)부터 t 번째 자리에 붙임. 원자 연산 없이 자리가 정해짐
    particleAliveOut[particleCounters[kCounterEmitAliveBase] + t] = index;
}

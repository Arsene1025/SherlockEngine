// GPU 파티클 (VFX) 공용: 이미터 상수 b8 (시뮬레이션·정렬·그리기 셋이 모두 꽂음), 씬 깊이 t23, 파티클 구조체, 난수.
// 상수버퍼와 구조체 배치는 Graphics/ShaderConstants.h 와 바이트 단위로 같아야 함.
// 시뮬레이션 셋은 ParticleSim.hlsli, 정렬 셋은 ParticleSort.hlsli.
#ifndef SHERLOCK_PARTICLE_HLSLI
#define SHERLOCK_PARTICLE_HLSLI

#include "Common.hlsli"

// b8: 이미터 하나. ShaderConstants.h 의 ParticleEmitterConstants 와 같은 순서 (144바이트 = 16 × 9). CPU 가 프레임에 한 번
// 올리고 (Renderer::UpdateParticleEmitters), 시뮬레이션 셋(CS)·정렬 셋(CS)과 그리기 셋(VS/PS)이 같은 버퍼를 꽂음.
// 괄호 안: 값의 출처 (ParticleEmitter 컴포넌트 속성 / 오브젝트 / 렌더러) → 읽는 셰이더.
// float3 다음에 float 하나를 붙여 16바이트 칸을 채움 (HLSL 상수 버퍼는 float4 단위로 묶임).
cbuffer ParticleEmitter : register(b8)
{
    float3 particleEmitterPosition;  // 이미터 월드 위치 = 소유 오브젝트 월드 행렬의 원점 (오브젝트) → Emit: 생성 위치의 중심
    float particleDeltaTime;         // 이번 프레임 시뮬레이션 시간 (초). --particle-step 이면 고정값, 아니면 실제 경과 시간을 [0, 0.1] 로 자름
                                     // (렌더러) → Sim: 적분, Emit: 프레임 안 출생 시각
    float3 particleDirection;        // 방출 방향 = 오브젝트 로컬 +Y 를 월드로 바꿔 정규화 (오브젝트) → Emit: 원뿔의 축
    float particleSpreadCos;         // cos(spreadAngle). 원뿔 반각의 코사인 — 1 이면 한 방향, −1 이면 모든 방향 (spreadAngle) → Emit
    float3 particleColor;            // 선형 RGB × intensity. 1 을 넘으면 HDR (블룸에 걸림) (color, intensity) → VS: 정점 색
    float particleLifetime;          // 수명 (초, ≥ 0.01). 태어날 때 파티클의 lifetime 에 복사됨 (lifetime) → Emit
    float3 particleGravity;          // 가속도 (m/s², 월드) (gravity) → Emit: 출생 시각만큼 미리 적용, Sim: 매 프레임 속도에 더함
    float particleEmitRadius;        // 생성 위치가 퍼지는 구의 반지름 (m) (emitRadius) → Emit
    float particleStartSpeed;        // 방출 속력 (m/s) (startSpeed) → Emit
    float particleSpeedVariation;    // 0..1. 속력 = startSpeed × (1 − variation × 난수) (speedVariation) → Emit
    float particleStartSize;         // 태어날 때 쿼드 한 변 (m) (startSize) → VS
    float particleEndSize;           // 수명 끝의 쿼드 한 변 (m). 수명 진행률로 선형 보간 (endSize) → VS
    uint particleCapacity;           // 파티클 버퍼 칸 수 = maxParticles 를 [1, 262144] 로 자른 값 (maxParticles) → Reset: 초기화 범위,
                                     // SortInit·SortStep: 정렬 길이 계산 (ParticleSortCount)
    uint particleSpawnCount;         // 이번 프레임에 태어나길 원하는 수 = spawnRate × dt + 지난 프레임에 남은 소수 부분의 정수 부분.
                                     // 사망 목록이 모자라면 Args 가 줄임 (spawnRate, 렌더러) → Args: 생성 수 결정, Emit: 출생 시각의 분모
    uint particleSeed;               // 난수 씨앗. 이미터마다 프레임마다 1 씩 늘어남 (렌더러) → Emit: PcgHash(t ^ PcgHash(seed))
    uint particleCollide;            // 1 이면 깊이 버퍼 충돌 (collide) → Sim: Collide 호출 여부
    float particleSoftDistance;      // 소프트 파티클 거리 (m). 표면과 이 거리보다 가까우면 투명해짐. 0 = 끔 (softness) → PS
    float particleBounce;            // 충돌 반발 계수 0..1. 0 = 표면을 따라 미끄러짐, 1 = 완전 탄성 (bounce) → Sim: 반사 속도
    float particleOpacity;           // 알파 모드의 불투명도 배율 0..1 (opacity) → PS
    uint particleBlendMode;          // 0 가산 (알파 0 을 내보냄), 1 알파 (프리멀티플라이드, 정렬해서 그림) (blendMode) → PS
    float3 particleEndTint;          // 수명 끝의 색 배율. 1 → endTint 로 선형 보간, 흰색이면 변화 없음 (endTint) → VS
    float particlePadding;           // 16바이트 칸 맞춤 (쓰지 않음)
    float3 particleWind;             // 바람 = 속도가 끌려가는 목표 (m/s, 월드) (wind) → Sim
    float particleDrag;              // 끌려가는 세기 (1/초). 0 = 끔 (drag) → Sim
}

// t23: 씬 깊이 (D24S8 의 깊이, DepthRead 상태 — 읽기 전용 DSV 와 동시에 읽음). 파티클 PS (소프트) · CS (충돌).
Texture2D<float> particleSceneDepth : register(t23);

// 깊이 버퍼 값 → 뷰 공간 z. a = 투영 [2][2], b = [3][2] (행 벡터 기준). 원근: d = a + b / z, 직교: d = a z + b.
float LinearViewDepth(float d, float a, float b, float perspective)
{
    return perspective > 0.5f ? b / (d - a) : (d - b) / a;
}

// 파티클 하나 (32바이트, ShaderConstants.h 의 kParticleStride). age >= lifetime 이면 죽은 파티클 — 그리지 않음.
// 버퍼를 새로 만든 프레임에 CS_ParticleReset 이 전부 (age 1, lifetime 0) 으로 지움.
struct Particle
{
    float3 position;   // 월드
    float age;         // 초
    float3 velocity;   // m/s, 월드
    float lifetime;    // 초. 태어날 때 이미터 값을 복사함
};

// t21 파티클 버퍼, t22 번호 목록. 그리기 셋 (VS) 과 정렬 셋 (CS) 이 같은 레지스터·같은 C++ 슬롯 (kSRVSlotParticles,
// kSRVSlotParticleAlive) 으로 꽂음. t22 는 정렬 셋과 가산 이미터의 그리기에서는 이번 프레임 생존 목록이고, 알파 이미터의
// 그리기에서는 그 뷰의 카메라로 먼 것부터 정렬한 번호 (앞 생존 수 칸이 유효).
//그리기(VS)·정렬(CS)이 읽기 전용으로 꽂는 입자 버퍼와 번호 목록
StructuredBuffer<Particle> particles : register(t21);
StructuredBuffer<uint> aliveList : register(t22);

// 파티클 개수 버퍼 (u4 particleCounters / 정렬 셋의 t24 sortCounters) 의 칸 번호.
static const uint kCounterAlive = 0;       // 이번 프레임 생존 목록에 지금까지 들어간 수 (시뮬레이션이 원자적으로 늘림)
static const uint kCounterDead = 1;        // 사망 목록 길이
static const uint kCounterSimCount = 2;    // 이번 프레임 시뮬레이션이 훑을 수 (= 지난 프레임 최종 생존 수)
static const uint kCounterEmitCount = 3;   // 이번 프레임 실제로 태어나는 수
static const uint kCounterEmitDeadBase = 4;  // 생성이 꺼내 쓸 사망 목록 구간의 시작
static const uint kCounterEmitAliveBase = 5; // 생성이 쓸 생존 목록 구간의 시작
static const uint kParticleCounterCount = 8; // ShaderConstants.h 의 kParticleCounterCount

// 파티클 VS → PS (ParticleVertexShader / ParticlePixelShader).
struct ParticleVSOutput
{
    float4 position : SV_POSITION;
    float2 corner : TEXCOORD0;    // 쿼드 안 위치 (−1..1). PS 가 원형 감쇠에 씀
    float3 color : TEXCOORD1;     // 수명에 따른 페이드까지 곱한 HDR 색
    float2 depthFade : TEXCOORD2; // x = 뷰 공간 z (소프트 파티클), y = 페이드 (알파 모드의 알파)
};

// PCG 해시 (Jarzynski & Olano 2020). 정수 하나 → 고르게 섞인 정수. 상태를 들고 다니지 않아 스레드마다
// (씨앗, 인덱스) 로 독립된 난수열을 만들 수 있음 — 같은 입력이면 같은 결과 (스크린샷 비교가 결정적).
//상태 없이 입력 하나로 섞으니까 스레드마다 (씨앗, t) 만 있으면 됨
uint PcgHash(uint v)
{
    const uint state = v * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// [0, 1) 난수. 상태를 한 번 섞고 위 24비트를 씀 (float 가수부에 정확히 들어감).
float Random01(inout uint state)
{
    state = PcgHash(state);
    return (state >> 8) * (1.0f / 16777216.0f);
}

#endif // SHERLOCK_PARTICLE_HLSLI

#include "ParticleSim.hlsli"

// GPU 파티클 시뮬레이션 (VFX 2단계). 지난 프레임 생존 목록만 훑음 — DispatchIndirect, 그룹 수는 지난 프레임의
// CS_ParticleArgs 가 인자 버퍼 바이트 16 에 써 둠. CPU 는 몇 개가 살아 있는지 모름.
//
// 적분한 뒤 살아 있으면 이번 프레임 생존 목록에, 죽었으면 사망 목록에 원자적으로 넣음. 넣는 순서는 스레드 실행
// 순서라 실행마다 다를 수 있음 (가산 블렌딩이라 그림은 같고, FP16 덧셈 순서 차이로 최하위 비트만 흔들릴 수 있음).
// 이번 프레임에 태어나는 파티클은 이 다음 CS_ParticleEmit 이 넣으므로 태어난 프레임에는 적분하지 않음 (1단계와 같음).
// 깊이 버퍼 한 점 (픽셀 중심) 의 월드 위치.
float3 SceneWorldPosition(int2 pixel)
{
    const float d = particleSceneDepth.Load(int3(pixel, 0));
    const float2 uv = (pixel + 0.5f) / float2(collisionDepthSize);
    const float4 h = mul(float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, d, 1.0f), collisionInvViewProj);
    return h.xyz / h.w;
}

// 화면 공간 충돌: 적분 뒤 위치를 메인 카메라로 투영해 그 픽셀의 깊이와 비교. 표면 뒤 thickness 안에 들어왔으면
// 이전 위치로 되돌리고, 깊이 버퍼 이웃 픽셀로 만든 법선 방향 속도를 반사함 (bounce 배). 화면 밖·카메라 뒤면 그대로.
//
// 두께는 시선 방향 깊이 차이가 아니라 표면 법선 방향 거리로 잼. 바닥을 비스듬히 (10°) 보면 바닥 아래 0.1 m 가 뷰 z 로는
// 0.1 / sin 10° ≈ 0.57 m 라, 뷰 z 로 재면 빠른 파티클이 한 스텝에 두께를 넘어 뚫고 지나감. 뷰 z 차이는 느슨한 상한
// (두께 × kGrazingDepthScale) 으로만 써서 멀리 뒤에 있는 물체 (실루엣 근처의 접평면에 우연히 가까운 점) 를 거름.
static const float kGrazingDepthScale = 5.0f;

void Collide(inout Particle p, float3 previousPosition)
{
    const float4 clip = mul(float4(p.position, 1.0f), collisionViewProj);
    if (clip.w <= 0.0f) return;
    const float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) >= 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f) return;
    const int2 size = int2(collisionDepthSize);
    const int2 pixel = min(int2(float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * float2(size)), size - 1);

    const float sceneZ = LinearViewDepth(particleSceneDepth.Load(int3(pixel, 0)), collisionDepthA, collisionDepthB, collisionPerspective);
    const float particleZ = LinearViewDepth(ndc.z, collisionDepthA, collisionDepthB, collisionPerspective);
    if (particleZ <= sceneZ || particleZ > sceneZ + collisionThickness * kGrazingDepthScale) return;

    // 법선: 이웃 픽셀 둘의 월드 위치 차이의 외적. 화면 끝에서는 반대쪽 이웃을 씀. 방향은 카메라를 향하게 맞춤
    // (픽셀 위치에서 그 픽셀의 근평면 점을 뺀 것이 시선 방향).
    const float3 center = SceneWorldPosition(pixel);
    const int2 dx = int2(pixel.x + 1 < size.x ? 1 : -1, 0);
    const int2 dy = int2(0, pixel.y + 1 < size.y ? 1 : -1);
    float3 n = cross(SceneWorldPosition(pixel + dx) - center, SceneWorldPosition(pixel + dy) - center);
    const float lengthSq = dot(n, n);
    if (lengthSq < 1e-12f) return;
    n *= rsqrt(lengthSq);
    const float2 uv = (pixel + 0.5f) / float2(size);
    const float4 nearH = mul(float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f), collisionInvViewProj);
    if (dot(n, center - nearH.xyz / nearH.w) > 0.0f) n = -n;
    if (dot(p.position - center, n) < -collisionThickness) return;   // 표면 접평면에서 법선 방향으로 두께보다 깊음

    p.position = previousPosition;
    const float vn = dot(p.velocity, n);
    if (vn < 0.0f) p.velocity -= (1.0f + particleBounce) * vn * n;
}

//입자를 움직이고 생사를 판정한다
[numthreads(64, 1, 1)]
void CS_ParticleSim(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= particleCounters[kCounterSimCount]) return;   // 마지막 그룹의 남는 스레드

    const uint index = particleAliveIn[i];
    Particle p = particlesRW[index];
    const float dt = particleDeltaTime;
    const float3 previousPosition = p.position;
    // 반암시적 오일러: 속도를 먼저 갱신하고 새 속도로 위치를 옮김.
    p.velocity += particleGravity * dt;
    p.position += p.velocity * dt;
    p.age += dt;
    if (particleCollide != 0) Collide(p, previousPosition);
    particlesRW[index] = p;

    uint slot;
    if (p.age < p.lifetime)
    {
        InterlockedAdd(particleCounters[kCounterAlive], 1, slot);
        particleAliveOut[slot] = index;
    }
    else
    {
        InterlockedAdd(particleCounters[kCounterDead], 1, slot);
        particleDead[slot] = index;
    }
}

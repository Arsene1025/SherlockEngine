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
    //픽셀 중심의 uv (0~1)
    const float2 uv = (pixel + 0.5f) / float2(collisionDepthSize);
    //uv → NDC (y 뒤집음), 깊이 d 를 z 로 넣고 역 viewProj 를 곱한 뒤 w 로 나눔
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
    //입자 위치를 메인 카메라 화면으로 투영
    const float4 clip = mul(float4(p.position, 1.0f), collisionViewProj);
    //카메라 뒤면 충돌 검사 안 함
    if (clip.w <= 0.0f) return;
    const float3 ndc = clip.xyz / clip.w;
    //화면 밖이거나 깊이 범위 밖이면 깊이 버퍼에 정보가 없어서 그대로 둠
    if (any(abs(ndc.xy) >= 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f) return;
    const int2 size = int2(collisionDepthSize);
    //NDC → 픽셀 좌표 (y 뒤집음), 마지막 픽셀을 넘지 않게 자름
    const int2 pixel = min(int2(float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f) * float2(size)), size - 1);

    //그 픽셀의 씬 표면과 입자의 뷰 공간 z
    const float sceneZ = LinearViewDepth(particleSceneDepth.Load(int3(pixel, 0)), collisionDepthA, collisionDepthB, collisionPerspective);
    const float particleZ = LinearViewDepth(ndc.z, collisionDepthA, collisionDepthB, collisionPerspective);
    //입자가 표면 앞이면 충돌 아님, 너무 멀리 뒤면 다른 물체 뒤를 지나는 것으로 보고 통과
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

    //충돌: 위치를 적분 전으로 되돌림
    p.position = previousPosition;
    //표면 쪽으로 들어가는 속도 성분(vn < 0)만 반사 → 그 성분이 -bounce × vn 이 됨
    //bounce 0 이면 그 성분만 없어져 표면을 따라 미끄러짐
    const float vn = dot(p.velocity, n);
    if (vn < 0.0f) p.velocity -= (1.0f + particleBounce) * vn * n;
}

//입자를 움직이고 생사를 판정한다
[numthreads(64, 1, 1)]
void CS_ParticleSim(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    //스레드 i = 지난 프레임 생존 목록의 i 번째 칸. SimCount = 지난 프레임 최종 생존 수
    if (i >= particleCounters[kCounterSimCount]) return;   // 마지막 그룹의 남는 스레드

    //생존 목록에서 칸 번호를 받아 그 칸의 입자를 읽음
    const uint index = particleAliveIn[i];
    Particle p = particlesRW[index];
    const float dt = particleDeltaTime;
    //충돌하면 되돌릴 위치
    const float3 previousPosition = p.position;
    // 반암시적 오일러: 속도를 먼저 갱신하고 새 속도로 위치를 옮김.
    p.velocity += particleGravity * dt;
    p.position += p.velocity * dt;
    //나이 증가. lifetime 을 넘으면 아래에서 죽음 처리
    p.age += dt;
    //깊이 충돌을 켠 이미터만
    if (particleCollide != 0) Collide(p, previousPosition);
    //죽어도 일단 써 둠 (age >= lifetime 이라 다시 태어나기 전까지 읽히지 않음)
    particlesRW[index] = p;

    //목록의 몇 번째 칸에 넣을지. InterlockedAdd 가 늘리기 전 값을 돌려줘서 스레드끼리 자리가 안 겹침
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

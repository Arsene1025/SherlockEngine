#include "Particle.hlsli"

// GPU 파티클 그리기 (VFX 2단계). 정점 버퍼 없이 DrawInstancedIndirect — 인스턴스 하나가 파티클 하나(쿼드 = 삼각형
// 2개, 정점 6개)이고 인스턴스 수는 GPU 가 센 생존 수 (CS_ParticleArgs 가 인자 버퍼에 씀). SV_InstanceID 로 생존 목록에서
// 파티클 번호를, SV_VertexID 로 쿼드 모서리를 정함.
//
//   b0  : PerFrame (view, proj — 뷰마다 다시 올라오므로 미리보기 뷰도 같은 드로우로 그려짐)
//   b8  : 이미터 상수 (크기·색)
//   t21 : 파티클 버퍼 (StructuredBuffer<Particle>)
//   t22 : 이번 프레임 생존 목록 (StructuredBuffer<uint>)
//
// 빌보드: 뷰 공간에서 중심에 모서리 오프셋을 더함 — 카메라의 오른쪽·위 축을 따로 뽑지 않아도 늘 화면을 향함.
// 알파 이미터는 t22 가 생존 목록이 아니라 이 뷰의 카메라로 먼 것부터 정렬한 목록 (앞 생존 수 칸이 유효).
// t21·t22 선언은 Particle.hlsli (정렬 셋과 같이 씀).

//쿼드 모서리 6 개 (삼각형 2 개). -1 ~ 1 범위
static const float2 kQuadCorners[6] =
{
    float2(-1.0f, -1.0f), float2(-1.0f, 1.0f), float2(1.0f, 1.0f),
    float2(-1.0f, -1.0f), float2(1.0f, 1.0f), float2(1.0f, -1.0f),
};

ParticleVSOutput VS_Particle(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    //인스턴스 번호 → 목록(생존 또는 정렬)에서 칸 번호 → 입자
    const Particle p = particles[aliveList[instanceId]];
    //정점 번호 → 쿼드의 어느 모서리인지
    const float2 corner = kQuadCorners[vertexId % 6];

    ParticleVSOutput output;
    output.corner = corner;
    //수명 진행률 0 ~ 1
    const float t = saturate(p.age / p.lifetime);   // 생존 목록에는 age < lifetime 인 것만 있음
    //크기를 수명에 따라 start → end 로 보간
    const float size = lerp(particleStartSize, particleEndSize, t);
    //뷰 공간으로 옮긴 뒤 xy 에 모서리를 더함 → 늘 카메라를 향하는 빌보드
    float4 viewPosition = mul(float4(p.position, 1.0f), view);
    viewPosition.xy += corner * (0.5f * size);
    //투영
    output.position = mul(viewPosition, proj);
    // 처음 10% 동안 켜지고 끝까지 선형으로 꺼짐 (갑자기 생기고 사라지는 깜빡임 방지).
    const float fade = smoothstep(0.0f, 0.1f, t) * (1.0f - t);
    output.color = particleColor * lerp(1.0f, particleEndTint, t) * fade;   // endTint 기본 (1,1,1) 이면 lerp 가 정확히 1
    //PS 로 넘김: 뷰 z (소프트 파티클), 페이드 (알파 모드의 알파)
    output.depthFade = float2(viewPosition.z, fade);
    return output;
}

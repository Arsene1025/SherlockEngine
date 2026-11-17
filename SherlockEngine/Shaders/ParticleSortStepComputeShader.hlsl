#include "ParticleSort.hlsli"

// 바이토닉 정렬 한 단계 (VFX 3단계). CPU 가 k = 2, 4, … N 마다 j = k/2, … 1 순서로 디스패치함 (N = 2^m 이면
// m(m+1)/2 번). (k, j) 는 단계마다 미리 만들어 둔 상수 버퍼 (b11) — 디스패치마다 다른 값을 Dynamic 버퍼 하나에
// 다시 올릴 수 없음 (D3D12 의 프레임당 한 번 규칙).
//
// 스레드 i 가 짝 l = i ^ j 와 비교 (l > i 인 쪽만 일함). i & k == 0 인 열은 내림차순, 아니면 오름차순으로 맞추면
// 마지막 단계 (k = N) 뒤 전체가 내림차순 (먼 것부터).
[numthreads(64, 1, 1)]
void CS_ParticleSortStep(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    //정렬 길이 밖 스레드는 일 없음
    if (i >= ParticleSortCount(particleCapacity)) return;
    //비교할 짝 = i 에서 j 비트를 뒤집은 칸 (거리 j)
    const uint l = i ^ sortJ;
    //짝 중 번호가 작은 쪽 스레드만 비교·교환 (같은 쌍을 두 번 바꾸지 않게)
    if (l <= i) return;
    const float a = sortKeys[i];
    const float b = sortKeys[l];
    //i 가 속한 길이 k 묶음의 방향. 묶음마다 내림·오름차순이 번갈아야 다음 k 에서 합쳐짐
    const bool descending = (i & sortK) == 0;
    //방향과 맞지 않으면 키와 번호를 함께 맞바꿈
    if (descending ? (a < b) : (a > b))
    {
        sortKeys[i] = b;
        sortKeys[l] = a;
        const uint t = sortIndices[i];
        sortIndices[i] = sortIndices[l];
        sortIndices[l] = t;
    }
}

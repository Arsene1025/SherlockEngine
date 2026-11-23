// SherlockEngine 공용 셰이더 헤더: 모든 셰이더가 쓰는 상수·b0 PerFrame·수학 함수만 둠.
// 나머지는 C++ 바인딩 셋 (Graphics/Renderer.cpp 의 CreateLayouts, Layout_*) 단위로 나눈 헤더에 있음 — 셋 하나의 선언은
// 그 셋의 헤더에만 두고, 셰이더 하나만 쓰는 슬롯은 그 셰이더 파일에 둠:
//   Lighting.hlsli      Layout_Frame 의 나머지 (조명·그림자·환경·IBL)
//   Mesh.hlsli          Layout_Object (b1, b7) 와 메시 정점 입출력
//   Material.hlsli      Layout_Material
//   Post.hlsli          Layout_Post · Layout_BlurCompute · Layout_BlurWeights 공용 (b4, t20)
//   Sky.hlsli           스카이박스 VS → PS
//   Particle.hlsli      파티클 공용 (b8, t23, Particle) · ParticleSim.hlsli 시뮬레이션 셋 · ParticleSort.hlsli 정렬 셋
//   Shading.hlsli       표면 조명 계산 (SurfaceData, 환경광, 조명 루프, 그림자) — 포워드·디퍼드 공용. 셋 선언 없음
//   Clusters.hlsli      Layout_Frame 의 클러스터 버퍼 (t30, t31) 와 픽셀 → 클러스터 계산
//   MaterialSurface.hlsli  재질 셋 → SurfaceData (포워드 BasicPS · G-buffer PS)
//   GBuffer.hlsli       Layout_GBuffer (t23 깊이, t25~t28 G-buffer, t29 SSAO) 와 G-buffer → SurfaceData
// 깊이 → 뷰 z·월드 위치·뷰 위치 복원과 옥타헤드럴 노멀 인코딩은 디퍼드 라이팅·SSAO 가 함께 쓰므로 이 파일에 둠.
//   Ssao.hlsli          Layout_Ssao (b12, t23, t26, t29)
// 셰이더가 include 하는 헤더는 (재귀로) .cso 신선도 검사와 핫리로드 감시 대상 (ShaderCompiler::ListIncludeFiles).
//
// 상수버퍼 레이아웃은 Graphics/ShaderConstants.h 와 바이트 단위로 같아야 함.
// C++ 쪽 static_assert와 Debug 빌드의 D3DReflect 대조로 이를 검사함.
//
// register(b#, space0) 표기는 SM 5.0 fxc가 error X3721로 거부함 (5.1 이상만 지원).
// space 는 SM 5.1/6.0 으로 올릴 때 넣음.
#ifndef SHERLOCK_COMMON_HLSLI
#define SHERLOCK_COMMON_HLSLI

static const float EPSILON = 0.0001f;
static const float PI = 3.14159265f;

// b0: 프레임에 한 번 (뷰마다 — 미리보기 패스가 다른 카메라로 다시 올림). VS와 PS 모두 읽음. (576바이트)
cbuffer PerFrame : register(b0)
{
    matrix view;
    matrix proj;
    matrix viewProj;
    float3 cameraPosition;
    float time;
    float4 shadowParams;      // x = 1/맵 크기, y = 깊이 바이어스, z = 세기(0..1), w = 사용 여부
    float4 debugParams;       // x = 디버그 뷰 (0 조명, 1 알베도, 2 노멀, 3 깊이, 4 그림자, 5 UV, 6 금속성/거칠기, 7 AO … 15), y = 깊이 범위
    float4 environmentParams; // x = IBL 세기, y = 반사 큐브 밉 수, z = 확산 조도를 읽을 밉, w = 환경 있음
    float4 outputParams;      // x = 노출, y = 톤매핑, z = BRDF LUT 있음
    float4 shadowExtra;       // x = 노말 섀도 오프셋 (노말맵이 그림자 UV 를 미는 배율)
    float4 iblTint;           // xyz = IBL 색 배율 (확산 조도·반사 프리필터에 곱함)
    matrix invViewProj;       // viewProj 의 역. 깊이 → 월드 위치 복원
    float4 screenParams;      // xy = 이 뷰의 렌더 타깃 크기, zw = 역수
    float4 clusterParams;     // x = 클러스터드 켜짐, y = 원근(1)/직교(0), z = near, w = far
    float4 clusterDepth;      // slice = floor(f(z) · x + y). 원근 f = log2, 직교 f = z
    float4 aoParams;          // x = SSAO 결과 있음, y = 0 min(재질 AO, SSAO) / 1 곱
    // 속도 버퍼·TAA. proj·viewProj·invViewProj 는 TAA 가 켜지면 지터가 들어간 값이고, 속도는 아래 지터 없는 두 행렬로 계산함
    matrix unjitteredViewProj;      // 이번 프레임, 지터 없음
    matrix prevUnjitteredViewProj;  // 지난 프레임, 지터 없음 (이 뷰에 지난 프레임이 없으면 unjitteredViewProj 와 같음)
    float4 temporalParams;          // xy = 이번 지터 (NDC), z = TAA 히스토리 유효, w = 지터 위상 (0~7)
}

// 깊이 버퍼 값(0..1) → 뷰 공간 z (카메라 앞쪽 거리). LH 투영: 원근 d = (P22 · z + P32) / z, 직교 d = P22 · z + P32.
// proj 는 C++ 의 행 벡터 행렬을 전치해서 올렸으므로 HLSL 의 proj[r][c] 가 C++ 의 M(r, c) 와 같음. 원근이면 proj[2][3] = 1.
float ViewDepthFromDevice(float deviceDepth)
{
    const float p22 = proj[2][2];
    const float p32 = proj[3][2];
    return proj[2][3] > 0.5f ? p32 / (deviceDepth - p22) : (deviceDepth - p32) / p22;
}

// ---- 옥타헤드럴 노멀 인코딩 (Cigolle et al. 2014, "A Survey of Efficient Representations for Independent Unit Vectors") ----
// 단위 구를 팔면체 |x| + |y| + |z| = 1 로 투영하고, 아래 반쪽(z < 0) 네 삼각형을 위 반쪽 정사각형의 바깥 모서리로
// 접어 넣음 → 노멀 하나가 [-1, 1]² 의 점 하나 (float2). FP16 두 채널에 그대로 담음 (0..1 로 옮기지 않음).
float2 OctWrap(float2 v)
{
    // fxc(SM 5.0) 의 벡터 삼항 연산자는 성분마다 고름 (HLSL 2021 의 select 와 같은 뜻)
    return (1.0f - abs(v.yx)) * (v >= 0.0f ? 1.0f : -1.0f);
}

float2 EncodeOct(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    return n.z >= 0.0f ? n.xy : OctWrap(n.xy);
}

float3 DecodeOct(float2 f)
{
    float3 n = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
    const float t = saturate(-n.z);
    n.xy += (n.xy >= 0.0f) ? -t : t;
    return normalize(n);
}

// 화면 픽셀(SV_Position.xy, 픽셀 중심 +0.5) + 깊이 버퍼 값 → 월드 위치.
// 픽셀 → UV → NDC (y 는 위가 +1) → (NDC.xy, depth, 1) 에 viewProj⁻¹ 를 곱하고 w 로 나눔 (원근 나눗셈을 되돌림).
float3 ReconstructWorldPosition(float2 pixel, float deviceDepth)
{
    const float2 uv = pixel * screenParams.zw;
    const float4 ndc = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, deviceDepth, 1.0f);
    const float4 world = mul(ndc, invViewProj);
    return world.xyz / world.w;
}

// 화면 UV (0..1, v 아래로) + 깊이 버퍼 값 → 뷰 공간 위치. z 는 ViewDepthFromDevice, xy 는 투영을 되돌림
// (원근 x_ndc = (P00 · x + P20 · z) / z, 직교 x_ndc = P00 · x + P30). SSAO 가 뷰 공간에서 반구를 만들 때 씀.
float3 ViewPositionFromDevice(float2 uv, float deviceDepth)
{
    const float z = ViewDepthFromDevice(deviceDepth);
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const bool perspective = proj[2][3] > 0.5f;
    const float x = perspective ? (ndc.x - proj[2][0]) * z / proj[0][0] : (ndc.x - proj[3][0]) / proj[0][0];
    const float y = perspective ? (ndc.y - proj[2][1]) * z / proj[1][1] : (ndc.y - proj[3][1]) / proj[1][1];
    return float3(x, y, z);
}

float3 SafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return (lengthSquared > EPSILON * EPSILON) ? value * rsqrt(lengthSquared) : fallback;
}

#endif // SHERLOCK_COMMON_HLSLI

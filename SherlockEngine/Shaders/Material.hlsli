// 재질 셋 (Layout_Material). 상수버퍼 배치는 Graphics/ShaderConstants.h 와 바이트 단위로 같아야 함.
//
//   b3 Material (VS 의 uvScale, PS), 알베도 t0, 노멀 t1, 금속성/거칠기 t2, AO t3, 발광 t4, 반사 세기 t5, 반사 색 t6,
//   클리어코트 t16~t18, 투과 t19, 디졸브 노이즈 t20, 재질 샘플러 s0. t8~t15 는 프레임 셋 (t13 은 후처리 셋의 입력), t7 은 다른 셋 (속도·조명 경계 구)
//   이 쓰는 번호라 클리어코트부터 t16 을 씀.
#ifndef SHERLOCK_MATERIAL_HLSLI
#define SHERLOCK_MATERIAL_HLSLI

// b3: 재질. VS(uvScale)와 PS 모두. (112바이트)
cbuffer Material : register(b3)
{
    float4 baseColor;
    float3 emissive;          // PBR: 발광 계수 (선형)
    float metallic;           // 0 = 유전체, 1 = 금속
    float2 uvScale;
    float unlit;
    float normalStrength;     // 노멀 맵 xy 배율. 0 = 무시
    float alphaCutoff;        // 알파 컷아웃. 0 = 끔
    float roughness;          // 지각적 거칠기. α = roughness²
    float occlusionStrength;  // AO 텍스처를 얼마나 따를지
    float ior;                // KHR_materials_ior. 1.5 = F0 0.04
    float3 specularColor;     // KHR_materials_specular: 비금속 F0 에 곱함
    float specularFactor;     // 비금속 반사 세기. F90 으로도 쓰임
    float clearcoatFactor;    // KHR_materials_clearcoat. 0 = 끔
    float clearcoatRoughness;
    float clearcoatNormalScale;
    float transmissionFactor; // KHR_materials_transmission. 0 = 끔
    float blended;            // 1 = 블렌딩 패스 재질. 출력 rgb 에 알파를 미리 곱함 (블렌드: One, InvSrcAlpha)
    float dissolveNoiseScale; // 디졸브 노이즈 UV 배율 (uvScale 을 곱한 UV 에 한 번 더 곱함)
    float materialPadding2;
    float materialPadding3;
}

Texture2D albedoTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D metallicRoughnessTexture : register(t2);   // G = 거칠기, B = 금속성 (glTF)
Texture2D occlusionTexture : register(t3);           // R = AO
Texture2D emissiveTexture : register(t4);            // sRGB
Texture2D specularTexture : register(t5);            // A = specularFactor 에 곱함 (KHR_materials_specular)
Texture2D specularColorTexture : register(t6);       // sRGB. specularColor 에 곱함
Texture2D clearcoatTexture : register(t16);          // R = clearcoatFactor 에 곱함 (KHR_materials_clearcoat)
Texture2D clearcoatRoughnessTexture : register(t17); // G = clearcoatRoughness 에 곱함
Texture2D transmissionTexture : register(t19);       // R = transmissionFactor 에 곱함 (KHR_materials_transmission)
Texture2D clearcoatNormalTexture : register(t18);    // 코팅 노멀 맵 (선형). 비면 평평한 노멀 = 기하 노멀
Texture2D dissolveNoiseTexture : register(t20);     // R = 디졸브 노이즈 [0, 1] (선형). 비면 builtin:noise (Dissolve.hlsli). t20 은 후처리 셋의
                                                     // 블러 가중치와 같은 번호지만 같은 패스에 묶이지 않음
SamplerState albedoSampler : register(s0);

#endif // SHERLOCK_MATERIAL_HLSLI

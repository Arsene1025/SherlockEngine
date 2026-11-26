// 포워드 메시 PS. 재질 → SurfaceData (MaterialSurface.hlsli) → 조명 (Shading.hlsli).
// 디퍼드 경로에서는 G-buffer 에 못 담는 재질(unlit·블렌딩·투과·코팅 노멀 맵)과 디버그 뷰 5(UV)만 이 셰이더로 그림.
// 조명 계산은 디퍼드 라이팅 PS(DeferredLightingPixelShader.hlsl)와 같은 함수를 씀.
#include "MaterialSurface.hlsli"
#include "Velocity.hlsli"

// SV_Target1 = 속도 (포워드 패스의 RT 1). 반투명 PSO 는 RT 1 의 쓰기 마스크가 0 이라 속도 출력이 버려짐 (GetPipelineFor) —
// 반투명은 속도를 쓰지 않음 (뒤 표면의 속도가 남음).
struct ForwardOutput
{
    float4 color : SV_Target0;
    float2 velocity : SV_Target1;
};

float4 ShadeForward(VSOutput input, bool isFrontFace);

ForwardOutput PS_Main(VSOutput input, bool isFrontFace : SV_IsFrontFace)
{
    ForwardOutput o;
    o.color = ShadeForward(input, isFrontFace);
    o.velocity = VelocityUV(input.currClip, input.prevClip);
    return o;
}

float4 ShadeForward(VSOutput input, bool isFrontFace)
{
    // 반환문을 하나로 둠 — 함수 중간의 return 이 여럿이면 fxc 가 X4000 (초기화 안 된 변수) 경고를 냄 (PS_Main 에서 부르는 함수라서).
    const float4 surface = MaterialBaseColor(input);
    const float3 albedo = surface.rgb;

    // 알파 컷아웃 (glTF MASK). 나뭇잎·격자처럼 알파로 구멍을 낸 재질에 씀.
    if (alphaCutoff > 0.0f)
    {
        clip(surface.a - alphaCutoff);
    }
    // 디졸브 (Dissolve.hlsli). 진행도 0 이면 아무것도 하지 않음.
    const float dissolveNoise = DissolveNoise(input.uv);
    DissolveClip(dissolveNoise);

    float4 result;
    // 감마 검증용: 조명 없이 알베도를 그대로 출력함. 128 회색 텍스처가 화면에서 128로 읽혀야 함.
    // HDR: 톤매핑이 톤 패스로 옮겨 가서 이 픽셀도 노출·ACES 를 거침 — 감마 검증은 톤매핑을 끄고 노출 1 에서 할 것.
    // 오브젝트별 강조도 받음 (기본색 섞기 + 번쩍임·가장자리 빛을 더함. 강조가 없으면 예전과 같은 값).
    if (unlit > 0.5f)
    {
        const float faceSign = isFrontFace ? 1.0f : -1.0f;
        const float3 geometricNormal = faceSign * SafeNormalize(input.normal, float3(0.0f, 1.0f, 0.0f));
        const float3 unlitColor = HighlightAlbedo(albedo) + HighlightEmission(geometricNormal, input.worldPosition) + DissolveEdgeEmission(dissolveNoise);
        result = blended > 0.5f ? float4(unlitColor * surface.a, surface.a) : float4(unlitColor, surface.a);
    }
    else
    {
        const SurfaceData s = EvaluateMaterialSurface(input, isFrontFace, surface);
        const float3 viewDirection = SafeNormalize(
            cameraPosition - input.worldPosition, float3(0.0f, 0.0f, -1.0f));

        // 디버그 뷰. 에디터의 Render Settings 에서 고름. 5 (UV) 만 여기서 — 나머지는 디퍼드와 같은 함수.
        const int debugView = (int)debugParams.x;
        const float viewZ = ViewDepthFromWorld(input.worldPosition);
        float3 debugColor = float3(0.0f, 0.0f, 0.0f);
        if (debugView == 5)
        {
            result = float4(frac(input.uv), 0.0f, 1.0f);
        }
        else if (SurfaceDebugColor(debugView, s, input.worldPosition, debugColor))
        {
            result = float4(debugColor, 1.0f);
        }
        else if (ClusterDebugColor(debugView, input.position.xy, viewZ, debugColor))
        {
            result = float4(debugColor, 1.0f);
        }
        else
        {
            // 환경광 (+ 클리어코트) + 발광, 그 위에 조명을 하나씩 더함 (클러스터드면 이 픽셀 클러스터의 지역광만).
            // 포워드에는 SSAO 를 쓰지 않음 — SSAO 는 G-buffer 노멀이 필요하고, 디퍼드 경로의 포워드 물체는 SSAO 뒤에 그려짐.
            const float3 environment = ShadeEnvironment(s, viewDirection, s.ao);
            const float3 finalColor = ShadeLights(s, input.worldPosition, viewDirection, input.position.xy, viewZ, environment + s.emission);

            // HDR: 선형 HDR 값을 그대로 FP16 타깃에 씀 (1 을 넘어도 잘리지 않음). 노출·ACES 는 TonemapPixelShader 가 함.
            // 블렌딩 패스 재질은 rgb 에 알파를 미리 곱해 내보냄 (블렌드: One, InvSrcAlpha). 투과로 비치는 몫은 시선 방향의 프레넬
            // 반사를 뺀 만큼만 알파에서 뺌 (비스듬히 볼수록 유리가 더 거울처럼 보임).
            if (blended > 0.5f)
            {
                const float NdotV = max(dot(s.normal, viewDirection), EPSILON);
                const float fresnelView = FresnelSchlick(s.F0, s.F90, NdotV).g;
                const float opacity = saturate(surface.a * (1.0f - s.transmission * (1.0f - fresnelView)));
                result = float4(finalColor * surface.a, opacity);
            }
            else
            {
                result = float4(finalColor, surface.a);
            }
        }
    }
    return result;
}

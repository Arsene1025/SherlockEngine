struct VSOutput
{
    float4 position : SV_POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
};

float4 PS_Main(VSOutput vsOutput) : SV_TARGET
{
    float3 normal = normalize(vsOutput.normal);
    
    //아직 Light클래스 추가 전이므로 일단 임시로 셰이더에 추가
    float3 toLight = normalize(float3(-0.45f, 0.75f, -0.55f));
    float diffuse = saturate(dot(normal, toLight));
    float lightAmount = 0.22f + diffuse * 0.78f;
    
    
    float3 finalColor = vsOutput.col.rgb * lightAmount;

    return float4(finalColor, vsOutput.col.a);
}
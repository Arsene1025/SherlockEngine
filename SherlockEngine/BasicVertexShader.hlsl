cbuffer ConstBuffer : register(b0)
{
    matrix mWorld;
    matrix mView;
    matrix mProj;
    matrix mWVP;
}

struct VSInput
{
    float4 position : POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 col : COLOR0;
    float3 normal : NORMAL0;
};

VSOutput VS_Main(VSInput vsInput)
{
    VSOutput vsOutput = (VSOutput)0;

    vsInput.position.w = 1.0f;

    vsOutput.position = mul(vsInput.position, mWVP);
    vsOutput.col = vsInput.col;
    vsOutput.normal = normalize(mul(float4(vsInput.normal, 0.0f), mWorld).xyz);

    return vsOutput;
}
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
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 col : COLOR0;
};

VSOutput VS_Main(VSInput vsInput)
{
    VSOutput vsOutput = (VSOutput)0;
    
    vsInput.position.w = 1.0f;
    
    //º¯È¯
    vsInput.position = mul(vsInput.position, mWVP);
    //vsInput.position = mul(vsInput.position, mWorld);
    //vsInput.position = mul(vsInput.position, mView);
    //vsInput.position = mul(vsInput.position, mProj);
    
    vsOutput.position = vsInput.position;
    vsOutput.col = vsInput.col;
    
    return vsOutput;
}
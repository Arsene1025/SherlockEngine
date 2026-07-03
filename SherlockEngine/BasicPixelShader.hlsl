struct VSOutput
{
    float4 position : SV_POSITION;
    float4 col : COLOR0;
};

float4 PS_Main(VSOutput vsOutput) : SV_TARGET
{
    return vsOutput.col;
}
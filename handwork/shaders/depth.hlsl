// Include common HLSL code.
#include "Common.hlsl"

struct VertexIn
{
	float3 PosL    : POSITION;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);

    // Transform to homogeneous clip space.
    vout.PosH = mul(posW, gViewProj);
	
    return vout;
}

VertexOut VSInst(VertexIn vin, uint instanceID : SV_InstanceID)
{
    VertexOut vout = (VertexOut) 0.0f;

    // Fetch instance data.
    InstanceData instData = gInstanceData[instanceID];
    float4x4 world = instData.World;

    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), world);

    // Transform to homogeneous clip space.
    vout.PosH = mul(posW, gViewProj);
	
    return vout;
}

// Output the depth value.
float4 PS(VertexOut pin) : SV_Target
{
    float depth = pin.PosH.z / pin.PosH.w;
    depth = clamp(depth, 0.0f, 1.0f);
    
    // Encode.
    float value = depth * 255.0f;
    float first = (uint) value / 255.0f;
    value = value - (uint) value;
    value *= 255.0f;
    float second = (uint) value / 255.0f;
    value = value - (uint) value;
    value *= 255.0f;
    float third = (uint) value / 255.0f;

    return float4(first, second, third, 1.0f);
}


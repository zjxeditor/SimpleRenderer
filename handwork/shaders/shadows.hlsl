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

// can use a NULL pixel shader for depth pass.
void PS(VertexOut pin) 
{
}



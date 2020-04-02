#include "Tonemap.h"

cbuffer RootConstants
{
	uint2 Resolution;
	uint FramesRendered;
	float ExposureMultiplier;
}

Texture2D InputTexture;
RWTexture2D<float4> OutputTexture;

float3 GammaCorrect(float3 color)
{
	return pow(color, float4(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2, 1));

}

#define ComputeRS \
    "RootConstants(num32BitConstants=4, b0),\
    DescriptorTable(UAV(u0, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL),\
    DescriptorTable(SRV(t0, numDescriptors=1), visibility=SHADER_VISIBILITY_ALL)"

[RootSignature(ComputeRS)]
[numthreads(1, 1, 1)]
void main( uint2 DTid : SV_DispatchThreadID )
{
	if (DTid.x >= Resolution.x || DTid.y >= Resolution.y) return;

	float FrameCount = InputTexture[float2(0, Resolution.y - 1)].x;
	float3 outputColor = InputTexture[DTid] / FrameCount;
	outputColor *= ExposureMultiplier;

	outputColor = Tonemap(outputColor);
	outputColor = GammaCorrect(outputColor);

	OutputTexture[DTid] = float4(outputColor, 1);
}
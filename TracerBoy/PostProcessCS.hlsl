#define HLSL
#include "Tonemap.h"
#include "SharedPostProcessStructs.h"

cbuffer RootConstants
{
	PostProcessConstants Constants;
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
	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;

	float FrameCount = InputTexture[float2(0, Constants.Resolution.y - 1)].x;
	float3 outputColor = InputTexture[DTid] / FrameCount;
	
	if (Constants.UseToneMapping)
	{
		outputColor *= Constants.ExposureMultiplier;
		outputColor = Tonemap(outputColor);
	}
	
	if (Constants.UseGammaCorrection)
	{
		outputColor = GammaCorrect(outputColor);
	}

	OutputTexture[DTid] = float4(outputColor, 1);
}
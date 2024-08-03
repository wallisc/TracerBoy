#define HLSL 1
#include "GenerateHistogramSharedShaderStructs.h"
#include "Tonemap.h"

Texture2D InputTexture : register(t0);
RWByteAddressBuffer LuminanceHistogram : register(u0);

cbuffer GenerateHistogramCB
{
	GenerateHistogramConstants Constants;
}

groupshared uint GroupHistogram[NUM_HISTOGRAM_BINS];

// https://alextardif.com/HistogramLuminance.html
uint LuminanceToHistogramIndex(float luminance)
{
	const float epsilon = 0.00001;
    if(luminance < epsilon)
    {
        return 0;
    }
    
	float minLogLuminance = -10.0f;
	float oneOverLogLuminanceRange = 1.0 / 12.0f;
    float logLuminance = saturate((log2(luminance) - Constants.minLogLuminance) * Constants.oneOverLogLuminanceRange);
    return (uint)(logLuminance * 254.0 + 1.0);
}

[numthreads(GENERATE_HISTOGRAM_THREAD_GROUP_WIDTH, GENERATE_HISTOGRAM_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID, uint Gid : SV_GroupIndex )
{
	// Clear the whole histogram. The threadgroup has been tailored to ensure that there's a thread 
	// per histogram bin
	GroupHistogram[Gid] = 0;
	GroupMemoryBarrierWithGroupSync();

	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;


	{
		float4 AccumulatedOutput = InputTexture[DTid.xy];
		float3 Color = AccumulatedOutput.rgb / AccumulatedOutput.a;

		float luminance = ColorToLuma(Color);
		uint histogramIndex = LuminanceToHistogramIndex(luminance);
		InterlockedAdd(GroupHistogram[histogramIndex], 1);
	}

	GroupMemoryBarrierWithGroupSync();

	LuminanceHistogram.InterlockedAdd(Gid * BYTE_ADDRESS_BUFFER_STRIDE, GroupHistogram[Gid]);
}
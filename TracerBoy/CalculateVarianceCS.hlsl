#define HLSL
#include "CalculateVarianceSharedShaderStructs.h"
#include "Tonemap.h"

StructuredBuffer<CachedLuminance> AOVCachedLuminance : register(t0);
Texture2D PathTracingOutput: register(t1);

RWTexture2D<float2> AOVSummedVariance: register(u0);

cbuffer CalculateVarianceCB
{
	CalculateVarianceConstants Constants;
}

[numthreads(CALCULATE_VARIANCE_THREAD_GROUP_WIDTH, CALCULATE_VARIANCE_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;

	CachedLuminance cachedLuminance = AOVCachedLuminance[DTid.x + DTid.y * Constants.Resolution.x];

	float FrameCount = PathTracingOutput[float2(0, Constants.Resolution.y - 1)].x;
	float3 meanLuma = ColorToLuma(PathTracingOutput[DTid.xy] / FrameCount);

	float summedVariance = 0.0f;
	for (uint i = 0; i < NUM_CACHED_LUMINANCE_VALUES; i++)
	{
		summedVariance += (meanLuma - cachedLuminance.Luminance[i]) * (meanLuma - cachedLuminance.Luminance[i]);
	}

	AOVSummedVariance[DTid.xy] = AOVSummedVariance[DTid.xy] + float2(summedVariance / float(NUM_CACHED_LUMINANCE_VALUES), 1.0);
}
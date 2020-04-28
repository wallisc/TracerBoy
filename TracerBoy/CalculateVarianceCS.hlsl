#define HLSL
#include "CalculateVarianceSharedShaderStructs.h"
#include "Tonemap.h"

Texture2D SummedLumaSquared: register(t0);
Texture2D PathTracingOutput: register(t1);

RWTexture2D<float2> LuminanceVariance : register(u0);

cbuffer CalculateVarianceCB
{
	CalculateVarianceConstants Constants;
}

[numthreads(CALCULATE_VARIANCE_THREAD_GROUP_WIDTH, CALCULATE_VARIANCE_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;

	float FrameCount = PathTracingOutput[float2(0, Constants.Resolution.y - 1)].x;
	float meanLuma = ColorToLuma(PathTracingOutput[DTid.xy] / FrameCount);
	float lumaSquared = SummedLumaSquared[DTid.xy] / FrameCount;
}
#define HLSL
#include "CalculateVarianceSharedShaderStructs.h"
#include "Tonemap.h"

Texture2D SummedLumaSquared: register(t0);
Texture2D PathTracingOutput: register(t1);

RWTexture2D<float4> LuminanceVariance : register(u0);

cbuffer CalculateVarianceCB
{
	CalculateVarianceConstants Constants;
}

[numthreads(CALCULATE_VARIANCE_THREAD_GROUP_WIDTH, CALCULATE_VARIANCE_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;

	float neighborVariance = 0.0;
	const uint cFramesToRelyOnNeighbor = 4;
	//if (Constants.GlobalFrameCount <= cFramesToRelyOnNeighbor)
	{
		const int kernelWidth = 5;
		uint count = 0;
		float summedLuma = 0.0;
		for (int x = -kernelWidth / 2; x < kernelWidth / 2; x++)
		{
			for (int y = -kernelWidth / 2; y < kernelWidth / 2; y++)
			{
				int2 sampleCoord = int2(DTid.xy)+int2(x, y);
				if (sampleCoord.x < 0 || sampleCoord.y < 0 ||
					sampleCoord.x >= Constants.Resolution.x || sampleCoord.y >= Constants.Resolution.y) continue;
				summedLuma += ColorToLuma(PathTracingOutput[sampleCoord].rgb / PathTracingOutput[sampleCoord].w);
				count++;
			}
		}
		float  lumaMean = summedLuma / count;

		float summedVariance = 0.0;
		for (int x = -kernelWidth / 2; x < kernelWidth / 2; x++)
		{
			for (int y = -kernelWidth / 2; y < kernelWidth / 2; y++)
			{
				int2 sampleCoord = int2(DTid.xy)+int2(x, y);
				if (sampleCoord.x < 0 || sampleCoord.y < 0 ||
					sampleCoord.x >= Constants.Resolution.x || sampleCoord.y >= Constants.Resolution.y) continue;
				summedVariance += (lumaMean - ColorToLuma(PathTracingOutput[sampleCoord] / PathTracingOutput[sampleCoord].w)) * (lumaMean - ColorToLuma(PathTracingOutput[sampleCoord] / PathTracingOutput[sampleCoord].w));
			}
		}

		neighborVariance = summedVariance / (count - 1);
	}	

	uint FrameCount = PathTracingOutput[DTid.xy].w;
	float meanLuma = ColorToLuma(PathTracingOutput[DTid.xy] / FrameCount);
	float lumaSquared = SummedLumaSquared[DTid.xy] / FrameCount;
	float temporalVariance = abs(lumaSquared - meanLuma * meanLuma);

	LuminanceVariance[DTid.xy] = float4(
		lerp(neighborVariance, temporalVariance, min(float(Constants.GlobalFrameCount - 1) / float(cFramesToRelyOnNeighbor), 1.0f)),
		temporalVariance,
		neighborVariance,
		0.0f);

}